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

  @Override
  public String getName() {
    return String.format("%s(xClusterConfig=%s)", super.getName(), taskParams().xClusterConfig);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    // Delete the config.
    xClusterConfig.delete();

    log.info("Completed {}", getName());
  }
}
