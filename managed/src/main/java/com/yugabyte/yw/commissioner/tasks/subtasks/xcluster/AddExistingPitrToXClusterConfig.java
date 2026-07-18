// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AddExistingPitrToXClusterConfig extends XClusterConfigTaskBase {

  @Inject
  protected AddExistingPitrToXClusterConfig(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The parent xCluster config must be stored in xClusterConfig field.
    // The pitr config to be added to associated xCluster config.
    public PitrConfig pitrConfig;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    log.info(
        "Reusing the existing PITR config {} for xClusterConfig {} because it has the right"
            + " parameters",
        taskParams().pitrConfig.getUuid(),
        xClusterConfig.getUuid());
    xClusterConfig.addPitrConfig(taskParams().pitrConfig);

    log.info("Completed {}", getName());
  }
}
