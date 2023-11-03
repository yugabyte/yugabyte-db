// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ResetXClusterConfigEntry extends XClusterConfigTaskBase {

  @Inject
  protected ResetXClusterConfigEntry(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s)", super.getName(), taskParams().getXClusterConfig());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    // Reset the config.
    xClusterConfig.reset();

    log.info("Completed {}", getName());
  }
}
