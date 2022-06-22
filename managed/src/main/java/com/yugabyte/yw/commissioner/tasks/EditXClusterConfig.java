// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.io.File;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class EditXClusterConfig extends XClusterConfigTaskBase {

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
        XClusterConfigStatusType initialStatus = xClusterConfig.status;
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
                      cert));

          createXClusterConfigRenameTask(editFormData.name)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

          // Delete the old directory if it created a new one.
          sourceCertificate.ifPresent(cert -> createTransferXClusterCertsRemoveTasks());
        } else if (editFormData.status != null) {
          createXClusterConfigSetStatusTask(initialStatus, editFormData.status)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        } else if (editFormData.tables != null) {
          Set<String> currentTableIds = xClusterConfig.getTables();
          Set<String> tableIdsToAdd =
              taskParams()
                  .editFormData
                  .tables
                  .stream()
                  .filter(tableId -> !currentTableIds.contains(tableId))
                  .collect(Collectors.toSet());
          // Save the to-be-added tables in the DB.
          xClusterConfig.addTables(tableIdsToAdd);
          Set<String> tableIdsToRemove =
              currentTableIds
                  .stream()
                  .filter(tableId -> !taskParams().editFormData.tables.contains(tableId))
                  .collect(Collectors.toSet());
          createXClusterConfigModifyTablesTask(tableIdsToAdd, tableIdsToRemove)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        } else {
          throw new RuntimeException("No edit operation was specified in editFormData");
        }

        // If the edit operation is not change status, set it to the initial status.
        if (editFormData.status == null) {
          createXClusterConfigSetStatusTask(initialStatus)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        createMarkUniverseUpdateSuccessTasks(targetUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.universeUUID)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } catch (Exception e) {
        log.error("{} hit error : {}", getName(), e.getMessage());
        setXClusterConfigStatus(XClusterConfigStatusType.Failed);
        throw new RuntimeException(e);
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.universeUUID);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.universeUUID);
    }

    log.info("Completed {}", getName());
  }
}
