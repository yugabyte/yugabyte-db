// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import lombok.extern.slf4j.Slf4j;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;

@Slf4j
public class RollbackYsqlMajorVersionCatalogUpgrade extends YsqlMajorUpgradeServerTaskBase {

  private final int MAX_ATTEMPTS = 20;
  private final long SLEEP_TIME_MS = 10000;

  public static class Params extends YsqlMajorUpgradeServerTaskBase.Params {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected RollbackYsqlMajorVersionCatalogUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    try {
      YsqlMajorCatalogUpgradeState state = getYsqlMajorCatalogUpgradeState();
      if (state.equals(
          YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_ROLLBACK_IN_PROGRESS)) {
        int attempts = 0;
        boolean isRollbackCompleted = false;
        while (attempts < MAX_ATTEMPTS) {
          state = getYsqlMajorCatalogUpgradeState();
          if (!state.equals(
              YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_ROLLBACK_IN_PROGRESS)) {
            isRollbackCompleted = true;
            break;
          }
          Thread.sleep(SLEEP_TIME_MS);
          attempts++;
        }
        if (!isRollbackCompleted) {
          log.error("YSQL major version catalog upgrade rollback did not complete in time.");
          throw new RuntimeException(
              "YSQL major version catalog upgrade rollback did not complete in time.");
        }
      }

      if (ImmutableList.of(
              YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK,
              YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK)
          .contains(state)) {
        rollbackYsqlMajorCatalogVersion();
      } else {
        log.warn(
            "Skipping starting YSQL major version catalog upgrade rollback as it is not in pending"
                + " rollback state");
        log.info("Current state: {}", state);
      }

      state = getYsqlMajorCatalogUpgradeState();
      if (!state.equals(YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING)) {
        log.error("YSQL major version catalog upgrade rollback did not complete successfully.");
        throw new RuntimeException(
            "YSQL major version catalog upgrade rollback did not complete successfully.");
      }
    } catch (Exception e) {
      log.error("Error while rolling back YSQL major version catalog upgrade: ", e);
      throw new RuntimeException(e);
    }
    log.info("Finished {}", getName());
  }
}
