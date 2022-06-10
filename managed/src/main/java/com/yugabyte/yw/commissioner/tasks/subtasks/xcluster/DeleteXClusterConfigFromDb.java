package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/** It creates a subtask to delete the xCluster config from Platform DB. */
@Slf4j
public class DeleteXClusterConfigFromDb extends XClusterConfigTaskBase {

  @Inject
  protected DeleteXClusterConfigFromDb(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Whether the xCluster config must be deleted even if it is not in Deleted state.
    public boolean forceDelete;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // The xClusterConfig field in taskParams must be set.
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    if (xClusterConfig == null) {
      throw new RuntimeException(
          "taskParams().xClusterConfig is null. Each DeleteXClusterConfig subtask must belong to "
              + "an xCluster config");
    }

    if (!taskParams().forceDelete
        && xClusterConfig.status != XClusterConfig.XClusterConfigStatusType.Deleted) {
      String errMsg =
          String.format(
              "xCluster config (%s) is in %s state, not in Deleted "
                  + "state; To delete it, you must use forceDelete",
              xClusterConfig.uuid, xClusterConfig.status);
      throw new RuntimeException(errMsg);
    }

    // Delete the config.
    xClusterConfig.delete();

    log.info("Completed {}", getName());
  }
}
