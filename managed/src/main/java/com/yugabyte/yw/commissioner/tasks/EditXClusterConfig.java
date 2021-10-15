// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
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

    XClusterConfigEditFormData editFormData = taskParams().editFormData;

    try {
      lockUniverseForUpdate(getUniverse().version);

      XClusterConfig xClusterConfig = getXClusterConfig();
      if (xClusterConfig.status != XClusterConfigStatusType.Running
          && xClusterConfig.status != XClusterConfigStatusType.Paused) {
        throw new RuntimeException(
            String.format(
                "XClusterConfig(%s) must be in `Running` or `Paused` state to edit",
                xClusterConfig.uuid));
      }

      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
      if (editFormData.name != null) {
        renameXClusterConfig();
      } else if (editFormData.status != null) {
        createXClusterConfigToggleStatusTask()
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      } else {
        createXClusterConfigModifyTablesTask()
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      }
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      subTaskGroupQueue.run();

      if (shouldIncrementVersion()) {
        getUniverse().incrementVersion();
      }

    } catch (Exception e) {
      setXClusterConfigStatus(XClusterConfigStatusType.Failed);
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      unlockUniverseForUpdate();
    }

    log.info("Completed {}", getName());
  }

  private void renameXClusterConfig() {
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    XClusterConfigEditFormData editFormData = taskParams().editFormData;

    log.info(
        "Renaming XClusterConfig({}): `{}` -> `{}`",
        xClusterConfig.uuid,
        xClusterConfig.name,
        editFormData.name);

    xClusterConfig.name = editFormData.name;
    xClusterConfig.update();
  }
}
