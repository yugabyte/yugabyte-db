package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.XClusterConfig;
import io.ebean.DB;
import io.ebean.Transaction;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/** It is the subtask to delete the xCluster config entry from the Platform DB. */
@Slf4j
public class DeleteXClusterConfigEntry extends XClusterConfigTaskBase {

  @Inject
  protected DeleteXClusterConfigEntry(
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

    try (Transaction transaction = DB.beginTransaction()) {
      // Promote a secondary xCluster config to primary if required.
      if (xClusterConfig.isUsedForDr() && !xClusterConfig.isSecondary()) {
        DrConfig drConfig = xClusterConfig.getDrConfig();
        drConfig.refresh();
        log.info(
            "DR config {} has {} xCluster configs",
            drConfig.getUuid(),
            drConfig.getXClusterConfigs().size());
        if (drConfig.getXClusterConfigs().size() > 1) {
          XClusterConfig secondaryXClusterConfig =
              drConfig.getXClusterConfigs().stream()
                  .filter(config -> !config.equals(xClusterConfig))
                  .findFirst()
                  .orElseThrow(() -> new IllegalStateException("No other xCluster config found"));
          secondaryXClusterConfig.setSecondary(false);
          secondaryXClusterConfig.update(); // Mark as primary (no longer secondary).
        }
      }

      // Delete the xClusterConfig.
      xClusterConfig.delete();

      // Commit the transaction.
      transaction.commit();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
