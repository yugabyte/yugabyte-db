package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PromoteSecondaryConfigToMainConfig extends XClusterConfigTaskBase {

  @Inject
  protected PromoteSecondaryConfigToMainConfig(
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

    if (!xClusterConfig.isUsedForDr()) {
      throw new IllegalArgumentException("The xCluster config is not used for DR");
    }

    // The following update happens in a transaction which ensure a dr config has exactly one
    // primary xCluster config.
    DrConfig drConfig = xClusterConfig.getDrConfig();
    drConfig.refresh();
    log.debug(
        "DR config {} has {} xCluster configs",
        drConfig.getUuid(),
        drConfig.getXClusterConfigs().size());
    drConfig
        .getXClusterConfigs()
        .forEach(config -> config.setSecondary(!config.equals(xClusterConfig)));
    drConfig.update();

    log.info("Completed {}", getName());
  }
}
