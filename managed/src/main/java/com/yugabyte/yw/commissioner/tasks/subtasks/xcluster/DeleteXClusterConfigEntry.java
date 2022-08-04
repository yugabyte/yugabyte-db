package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/** It is the subtask to delete the xCluster config entry from the Platform DB. */
@Slf4j
public class DeleteXClusterConfigEntry extends XClusterConfigTaskBase {

  @Inject
  protected DeleteXClusterConfigEntry(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Whether the xCluster config must be deleted even if it is not in Deleted state.
    public boolean forceDelete;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s,forceDelete=%s)",
        super.getName(), taskParams().xClusterConfig, taskParams().forceDelete);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    if (!isInMustDeleteStatus(xClusterConfig) && !taskParams().forceDelete) {
      String errMsg =
          String.format(
              "xCluster config (%s) is in %s state, not in a MustDelete "
                  + "state; To delete it, you must use forceDelete",
              xClusterConfig.uuid, xClusterConfig.status);
      throw new RuntimeException(errMsg);
    }

    // Delete the config.
    xClusterConfig.delete();

    log.info("Completed {}", getName());
  }
}
