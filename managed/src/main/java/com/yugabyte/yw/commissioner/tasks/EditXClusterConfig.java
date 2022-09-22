// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.io.File;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
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
    XClusterConfigEditFormData editFormData = taskParams().editFormData;

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
          Set<String> currentTableIds = xClusterConfig.getTables();
          Pair<Set<String>, Set<String>> tableIdsToAddTableIdsToRemovePair =
              getTableIdsDiff(currentTableIds, editFormData.tables);
          Set<String> tableIdsToAdd = tableIdsToAddTableIdsToRemovePair.getFirst();
          Set<String> tableIdsToRemove = tableIdsToAddTableIdsToRemovePair.getSecond();

          if (!tableIdsToAdd.isEmpty()) {
            // Todo: After having states for tables, do the following statement in the controller.
            // Save the to-be-added tables in the DB.
            if (editFormData.bootstrapParams != null) {
              xClusterConfig.addTables(tableIdsToAdd, editFormData.bootstrapParams.tables);
            } else {
              xClusterConfig.addTables(tableIdsToAdd);
            }

            Map<String, List<String>> mainTableIndexTablesMap =
                getMainTableIndexTablesMap(sourceUniverse, tableIdsToAdd);
            addIndexTables(tableIdsToAdd, mainTableIndexTablesMap);

            List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
                getRequestedTableInfoList(tableIdsToAdd, sourceUniverse, targetUniverse);

            addSubtasksToAddTablesToXClusterConfig(
                sourceUniverse, targetUniverse, requestedTableInfoList, mainTableIndexTablesMap);
          }

          if (!tableIdsToRemove.isEmpty()) {
            // Remove index tables as well.
            tableIdsToRemove.addAll(
                getMainTableIndexTablesMap(sourceUniverse, tableIdsToRemove)
                    .values()
                    .stream()
                    .flatMap(List::stream)
                    .filter(currentTableIds::contains)
                    .collect(Collectors.toSet()));
            createXClusterConfigModifyTablesTask(null /* tableIdsToAdd */, tableIdsToRemove)
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
        taskParams().editFormData.bootstrapParams,
        requestedNamespaceNameTablesInfoMapNeedBootstrap,
        true /* isReplicationConfigCreated */);
  }
}
