// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import lombok.extern.slf4j.Slf4j;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;

@Slf4j
public class RunYsqlMajorVersionCatalogUpgrade extends YsqlMajorUpgradeServerTaskBase {

  public static class Params extends YsqlMajorUpgradeServerTaskBase.Params {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected RunYsqlMajorVersionCatalogUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    try {
      if (isUpgradeAlreadyCompleted()) {
        log.info("Skipping YSQL major version catalog upgrade as it is already completed");
        return;
      }

      YsqlMajorCatalogUpgradeState state = getYsqlMajorCatalogUpgradeState();
      if (state.equals(YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK)) {
        log.info("Rolling back YSQL major version catalog upgrade as it failed previously");
        rollbackYsqlMajorCatalogVersion();
        state = getYsqlMajorCatalogUpgradeState();
      }

      if (state.equals(YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_IN_PROGRESS)) {
        log.info("Skipping YSQL major version catalog upgrade as it is already in progress");
      } else {
        log.info("Starting YSQL major version catalog upgrade");
        startYsqlMajorCatalogUpgrade();
      }
      waitForCatalogUpgradeToFinish();
    } catch (Exception e) {
      log.error("Error running ysql major version catalog upgrade: ", e);
      throw new RuntimeException(e);
    }
    log.info("Finished {}", getName());
  }
}
