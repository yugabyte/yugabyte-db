// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.io.File;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.springframework.util.CollectionUtils;
import org.yb.master.MasterDdlOuterClass;

@Slf4j
public class EditXClusterConfig extends CreateXClusterConfig {

  @Inject
  protected EditXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    XClusterConfigEditFormData editFormData = taskParams().getEditFormData();

    // Lock the source universe.
    lockUniverseForUpdate(sourceUniverse.universeUUID, sourceUniverse.version);
    try {
      // Lock the target universe.
      lockUniverseForUpdate(targetUniverse.universeUUID, targetUniverse.version);
      try {
        createXClusterConfigSetStatusTask(XClusterConfigStatusType.Updating)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        if (editFormData.name != null) {
          // If TLS root certificates are different, create a directory containing the source
          // universe root certs with the new name.
          Optional<File> sourceCertificate =
              getSourceCertificateIfNecessary(sourceUniverse, targetUniverse);
          sourceCertificate.ifPresent(
              cert ->
                  createTransferXClusterCertsCopyTasks(
                      targetUniverse.getNodes(),
                      XClusterConfig.getReplicationGroupName(
                          xClusterConfig.sourceUniverseUUID, editFormData.name),
                      cert,
                      targetUniverse.getUniverseDetails().getSourceRootCertDirPath()));

          createXClusterConfigRenameTask(editFormData.name)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

          // Delete the old directory if it created a new one.
          sourceCertificate.ifPresent(cert -> createTransferXClusterCertsRemoveTasks());
        } else if (editFormData.status != null) {
          createSetReplicationPausedTask(editFormData.status)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        } else if (editFormData.tables != null) {
          if (!CollectionUtils.isEmpty(taskParams().getTableInfoList())) {
            createXClusterConfigSetStatusForTablesTask(
                taskParams().getTableIdsToAdd(), XClusterTableConfig.Status.Updating);
            addSubtasksToAddTablesToXClusterConfig(
                sourceUniverse,
                targetUniverse,
                taskParams().getTableInfoList(),
                taskParams().getMainTableIndexTablesMap());
          }

          if (!CollectionUtils.isEmpty(taskParams().getTableIdsToRemove())) {
            createXClusterConfigSetStatusForTablesTask(
                taskParams().getTableIdsToRemove(), XClusterTableConfig.Status.Updating);
            createXClusterConfigModifyTablesTask(
                    taskParams().getTableIdsToRemove(),
                    XClusterConfigModifyTables.Params.Action.DELETE)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
          }
        } else {
          throw new RuntimeException("No edit operation was specified in editFormData");
        }

        createXClusterConfigSetStatusTask(XClusterConfigStatusType.Running)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.universeUUID);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      setXClusterConfigStatus(XClusterConfigStatusType.Running);
      if (editFormData.tables != null) {
        // Set tables in updating status to failed.
        Set<String> tablesInPendingStatus =
            xClusterConfig.getTableIdsInStatus(
                Stream.concat(
                        getTableIds(taskParams().getTableInfoList()).stream(),
                        taskParams().getTableIdsToRemove().stream())
                    .collect(Collectors.toSet()),
                ImmutableList.of(
                    XClusterTableConfig.Status.Updating, XClusterTableConfig.Status.Bootstrapping));
        xClusterConfig.setStatusForTables(tablesInPendingStatus, XClusterTableConfig.Status.Failed);
      }
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.universeUUID);
    }

    log.info("Completed {}", getName());
  }

  protected void addSubtasksToAddTablesToXClusterConfig(
      Universe sourceUniverse,
      Universe targetUniverse,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap) {
    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        requestedNamespaceNameTablesInfoMapNeedBootstrap =
            getRequestedNamespaceNameTablesInfoMapNeedBootstrap(
                taskParams().getTableIdsToAdd(), requestedTableInfoList, mainTableIndexTablesMap);

    // Replication for tables that do NOT need bootstrapping.
    Set<String> tableIdsNotNeedBootstrap =
        getTableIdsNotNeedBootstrap(taskParams().getTableIdsToAdd());
    if (!tableIdsNotNeedBootstrap.isEmpty()) {
      log.info(
          "Creating a subtask to modify replication to add tables without bootstrap for "
              + "tables {}",
          tableIdsNotNeedBootstrap);
      createXClusterConfigModifyTablesTask(
              tableIdsNotNeedBootstrap, XClusterConfigModifyTables.Params.Action.ADD)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
    }

    // YSQL tables replication with bootstrapping can only be set up with DB granularity. The
    // following subtasks remove tables in replication, so the replication can be set up again
    // for all the tables in the DB including the new tables.
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Set<String> tableIdsDeleteReplication = new HashSet<>();
    requestedNamespaceNameTablesInfoMapNeedBootstrap.forEach(
        (namespaceName, tablesInfo) -> {
          Set<String> tableIdsNeedBootstrap = getTableIds(tablesInfo);
          Set<String> tableIdsNeedBootstrapInReplication =
              xClusterConfig.getTableIdsWithReplicationSetup(
                  tableIdsNeedBootstrap, true /* done */);
          tableIdsDeleteReplication.addAll(tableIdsNeedBootstrapInReplication);
        });

    // A replication group with no tables in it cannot exist in YBDB. If all the tables must be
    // removed from the replication group, remove the replication group.
    boolean isRestartWholeConfig =
        tableIdsDeleteReplication.size()
            >= xClusterConfig.getTableIdsWithReplicationSetup().size()
                + tableIdsNotNeedBootstrap.size();
    log.info(
        "tableIdsDeleteReplication is {} and isRestartWholeConfig is {}",
        tableIdsDeleteReplication,
        isRestartWholeConfig);
    if (isRestartWholeConfig) {
      // Delete the xCluster config.
      createDeleteXClusterConfigSubtasks(
          xClusterConfig, true /* keepEntry */, taskParams().isForced());

      createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Updating);

      createXClusterConfigSetStatusForTablesTask(
          tableIdsDeleteReplication, XClusterTableConfig.Status.Updating);

      // Support mismatched TLS root certificates.
      Optional<File> sourceCertificate =
          getSourceCertificateIfNecessary(sourceUniverse, targetUniverse);
      sourceCertificate.ifPresent(
          cert ->
              createTransferXClusterCertsCopyTasks(
                  targetUniverse.getNodes(),
                  xClusterConfig.getReplicationGroupName(),
                  cert,
                  targetUniverse.getUniverseDetails().getSourceRootCertDirPath()));

      // Add the subtasks to set up replication for tables that need bootstrapping.
      addSubtasksForTablesNeedBootstrap(
          sourceUniverse,
          targetUniverse,
          taskParams().getBootstrapParams(),
          requestedNamespaceNameTablesInfoMapNeedBootstrap,
          false /* isReplicationConfigCreated */);
    } else {
      createXClusterConfigModifyTablesTask(
          tableIdsDeleteReplication,
          XClusterConfigModifyTables.Params.Action.REMOVE_FROM_REPLICATION_ONLY);

      createXClusterConfigSetStatusForTablesTask(
          tableIdsDeleteReplication, XClusterTableConfig.Status.Updating);

      // Add the subtasks to set up replication for tables that need bootstrapping.
      addSubtasksForTablesNeedBootstrap(
          sourceUniverse,
          targetUniverse,
          taskParams().getBootstrapParams(),
          requestedNamespaceNameTablesInfoMapNeedBootstrap,
          true /* isReplicationConfigCreated */);
    }
  }
}
