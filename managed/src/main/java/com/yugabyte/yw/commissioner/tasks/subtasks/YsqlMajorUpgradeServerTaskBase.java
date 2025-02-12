// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import java.time.Duration;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.FinalizeYsqlMajorCatalogUpgradeResponse;
import org.yb.client.GetYsqlMajorCatalogUpgradeStateResponse;
import org.yb.client.IsYsqlMajorCatalogUpgradeDoneResponse;
import org.yb.client.RollbackYsqlMajorCatalogVersionResponse;
import org.yb.client.StartYsqlMajorCatalogUpgradeResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterAdminOuterClass;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;
import org.yb.master.MasterTypes.MasterErrorPB;

/**
 * Base class for tasks related to YSQL major version catalog upgrades. This class provides methods
 * to start, check the status, finalize, and rollback YSQL major version catalog upgrades.
 *
 * <p>The catalog upgrade task states can be found in the master_admin.proto file: <a
 * href="https://github.com/yugabyte/yugabyte-db/blob/819ddd1af4e9be5413eb7f545c123ed0f71d39b4/src/yb/master/master_admin.proto#L208">
 * YSQL Major Catalog Upgrade States</a>
 */
@Slf4j
public abstract class YsqlMajorUpgradeServerTaskBase extends ServerSubTaskBase {

  private static final int MAX_ATTEMPTS = 20;
  private static final int DELAY_BETWEEN_ATTEMPTS_SEC = 60; // 1 minute

  public static class Params extends ServerSubTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Inject
  protected YsqlMajorUpgradeServerTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected boolean isUpgradeAlreadyCompleted() throws Exception {
    YsqlMajorCatalogUpgradeState state = getYsqlMajorCatalogUpgradeState();
    if (state.equals(
        MasterAdminOuterClass.YsqlMajorCatalogUpgradeState
            .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK)) {
      log.debug("YSQL major version catalog upgrade is finished and pending finalize or rollback.");
      return true;
    } else if (state.equals(
        MasterAdminOuterClass.YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_DONE)) {
      log.debug("YSQL major version catalog upgrade is already completed, or not required.");
      return true;
    }
    return false;
  }

  protected void startYsqlMajorCatalogUpgrade() throws Exception {
    try (YBClient client = getClient()) {
      StartYsqlMajorCatalogUpgradeResponse resp = client.startYsqlMajorCatalogUpgrade();
      if (resp.hasError()) {
        MasterErrorPB errorPB = resp.getServerError();
        log.error("Error while starting YSQL major version catalog upgrade: ", errorPB);
        throw new RuntimeException(
            " Error while starting YSQL major version catalog upgrade: " + errorPB.toString());
      }
      log.debug("Successfully started YSQL major version catalog upgrade");
    }
  }

  protected void waitForCatalogUpgradeToFinish() throws Exception {
    int attempts = 0;
    while (attempts < MAX_ATTEMPTS) {
      attempts++;
      log.debug(
          "Waiting for YSQL major version catalog upgrade to finish. Attempt {} of {}",
          attempts,
          MAX_ATTEMPTS);
      waitFor(Duration.ofSeconds(DELAY_BETWEEN_ATTEMPTS_SEC));
      try (YBClient client = getClient()) {
        IsYsqlMajorCatalogUpgradeDoneResponse resp = client.isYsqlMajorCatalogUpgradeDone();
        if (resp.hasError()) {
          MasterErrorPB errorPB = resp.getServerError();
          log.error("Error while checking YSQL major version catalog upgrade status: ", errorPB);
          throw new RuntimeException(
              " Error while checking YSQL major version catalog upgrade status: "
                  + errorPB.toString());
        }
        if (resp.isDone()) {
          log.debug("YSQL major version catalog upgrade is done");
          return;
        }
      }
    }
    throw new RuntimeException("YSQL major version catalog upgrade did not finish in time");
  }

  protected YsqlMajorCatalogUpgradeState getYsqlMajorCatalogUpgradeState() throws Exception {
    try (YBClient client = getClient()) {
      GetYsqlMajorCatalogUpgradeStateResponse resp = client.getYsqlMajorCatalogUpgradeState();
      if (resp.hasError()) {
        log.error("Error while getting YSQL major version catalog upgrade state: ", resp);
        throw new RuntimeException(
            "Error while getting YSQL major version catalog upgrade state: "
                + resp.getServerError().toString());
      }
      return resp.getState();
    }
  }

  protected void rollbackYsqlMajorCatalogVersion() throws Exception {
    try (YBClient client = getClient()) {
      RollbackYsqlMajorCatalogVersionResponse resp = client.rollbackYsqlMajorCatalogVersion();
      if (resp.hasError()) {
        log.error("Error while rolling back YSQL major version catalog upgrade: ", resp);
        throw new RuntimeException(
            "Error while rolling back YSQL major version catalog upgrade: "
                + resp.getServerError().toString());
      }
      log.debug("Successfully rolled back YSQL major version catalog upgrade");
    }
  }

  protected void finalizeYsqlMajorCatalogUpgrade() throws Exception {
    try (YBClient client = getClient()) {
      FinalizeYsqlMajorCatalogUpgradeResponse resp = client.finalizeYsqlMajorCatalogUpgrade();
      if (resp.hasError()) {
        log.error("Error while finalizing YSQL major version catalog upgrade: ", resp);
        throw new RuntimeException(
            "Error while finalizing YSQL major version catalog upgrade: "
                + resp.getServerError().toString());
      }
    } catch (Exception e) {
      log.error("Error while finalizing YSQL major version catalog upgrade: ", e);
      throw new RuntimeException(e);
    }
    log.debug("Successfully finalized YSQL major version catalog upgrade");
  }
}
