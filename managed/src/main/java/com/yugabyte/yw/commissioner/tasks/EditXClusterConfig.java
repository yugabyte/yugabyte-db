// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
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

    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    if (xClusterConfig == null) {
      throw new RuntimeException("xClusterConfig in task params cannot be null");
    }

    lockUniverseForUpdate(getUniverse().version);
    try {
      XClusterConfigEditFormData editFormData = taskParams().editFormData;

      XClusterConfigStatusType initialStatus = xClusterConfig.status;
      createXClusterConfigSetStatusTask(XClusterConfigStatusType.Updating)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      if (editFormData.name != null) {
        createXClusterConfigRenameTask(editFormData.name)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
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

      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      getRunnableTask().runSubTasks();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      setXClusterConfigStatus(XClusterConfigStatusType.Failed);
      throw new RuntimeException(e);
    } finally {
      unlockUniverseForUpdate();
    }

    log.info("Completed {}", getName());
  }
}
