// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.io.File;
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
            xClusterConfig.setStatusForTables(
                getTableIds(taskParams().getTableInfoList()), XClusterTableConfig.Status.Updating);
            addSubtasksToAddTablesToXClusterConfig(
                sourceUniverse,
                targetUniverse,
                taskParams().getTableInfoList(),
                taskParams().getMainTableIndexTablesMap());
          }

          if (!CollectionUtils.isEmpty(taskParams().getTableIdsToRemove())) {
            xClusterConfig.setStatusForTables(
                taskParams().getTableIdsToRemove(), XClusterTableConfig.Status.Updating);
            createXClusterConfigModifyTablesTask(
                    null /* tableIdsToAdd */, taskParams().getTableIdsToRemove())
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
        Set<String> tablesInUpdatingStatus =
            xClusterConfig.getTableIdsInStatus(
                Stream.concat(
                        getTableIds(taskParams().getTableInfoList()).stream(),
                        taskParams().getTableIdsToRemove().stream())
                    .collect(Collectors.toSet()),
                XClusterTableConfig.Status.Updating);
        xClusterConfig.setStatusForTables(
            tablesInUpdatingStatus, XClusterTableConfig.Status.Failed);
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
                requestedTableInfoList, mainTableIndexTablesMap);

    // Replication for tables that do NOT need bootstrapping.
    Set<String> tableIdsNotNeedBootstrap =
        getTableIdsNotNeedBootstrap(getTableIds(requestedTableInfoList));
    if (!tableIdsNotNeedBootstrap.isEmpty()) {
      log.info(
          "Creating a subtask to modify replication to add tables without bootstrap for "
              + "tables {}",
          tableIdsNotNeedBootstrap);
      createXClusterConfigModifyTablesTask(tableIdsNotNeedBootstrap, null /* tableIdsToRemove */)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
    }

    // Add the subtasks to set up replication for tables that need bootstrapping.
    addSubtasksForTablesNeedBootstrap(
        sourceUniverse,
        targetUniverse,
        taskParams().getBootstrapParams(),
        requestedNamespaceNameTablesInfoMapNeedBootstrap,
        true /* isReplicationConfigCreated */);
  }
}
