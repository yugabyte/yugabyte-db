// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import lombok.extern.slf4j.Slf4j;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;

@Slf4j
public class FinalizeYsqlMajorCatalogUpgrade extends YsqlMajorUpgradeServerTaskBase {

  public static class Params extends YsqlMajorUpgradeServerTaskBase.Params {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected FinalizeYsqlMajorCatalogUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    try {
      YsqlMajorCatalogUpgradeState state = getYsqlMajorCatalogUpgradeState();
      if (state.equals(
          YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK)) {
        finalizeYsqlMajorCatalogUpgrade();
      } else {
        log.info(
            "Skipping starting ysql major version catalog upgrade finalize as it is not in pending"
                + " finalize state. Current state: {}",
            state);
      }
      state = getYsqlMajorCatalogUpgradeState();
      if (!state.equals(YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_DONE)) {
        log.error(
            "Did not complete finalize the major version catalog upgrade, current state: {}",
            state);
        throw new RuntimeException("Did not complete finalize the major version catalog upgrade.");
      }
    } catch (Exception e) {
      log.error("Error while finalizing ysql major version catalog upgrade: ", e);
      throw new RuntimeException(e);
    }
    log.info("Finished {}", getName());
  }
}
