// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.YbcUpgrade;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.UUID;

public class UpgradeYbc extends AbstractTaskBase {

  private final YbcUpgrade ybcUpgrade;
  private final YbcClientService ybcClientService;

  @Inject
  protected UpgradeYbc(
      BaseTaskDependencies baseTaskDependencies,
      YbcUpgrade ybcUpgrade,
      YbcClientService ybcClientService) {
    super(baseTaskDependencies);
    this.ybcUpgrade = ybcUpgrade;
    this.ybcClientService = ybcClientService;
  }

  public static class Params extends AbstractTaskParams {
    public UUID universeUUID;
    public String ybcVersion;
    public boolean validateOnlyMasterLeader = false;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  private void preChecks(Universe universe, String ybcVersion) {
    if (!universe.getUniverseDetails().enableYbc) {
      throw new RuntimeException(
          "Cannot upgrade YBC as it is not enabled on universe " + universe.universeUUID);
    }
    if (universe.getUniverseDetails().ybcSoftwareVersion.equals(ybcVersion)) {
      throw new RuntimeException(
          "YBC version "
              + ybcVersion
              + " is already installed on universe "
              + universe.universeUUID);
    }
  }

  @Override
  public void run() {
    try {
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      preChecks(universe, taskParams().ybcVersion);
      ybcUpgrade.upgradeYBC(taskParams().universeUUID, taskParams().ybcVersion);
      int numRetries = 0;
      while (numRetries < ybcUpgrade.MAX_YBC_UPGRADE_POLL_RESULT_TRIES) {
        numRetries++;
        if (!ybcUpgrade.checkYBCUpgradeProcessExists(taskParams().universeUUID)) {
          break;
        } else {
          ybcUpgrade.pollUpgradeTaskResult(
              taskParams().universeUUID, taskParams().ybcVersion, false);
        }
        waitFor(Duration.ofMillis(ybcUpgrade.YBC_UPGRADE_POLL_RESULT_SLEEP_MS));
      }

      if (!ybcUpgrade.pollUpgradeTaskResult(
          taskParams().universeUUID, taskParams().ybcVersion, true)) {
        if (numRetries == ybcUpgrade.MAX_YBC_UPGRADE_POLL_RESULT_TRIES) {
          throw new RuntimeException("YBC upgrade task did not complete in expected time.");
        } else if (!taskParams().validateOnlyMasterLeader) {
          throw new RuntimeException(
              "YBC Upgrade task failed as ybc does not upgraded on master leader.");
        }
      }

      String sourceYbcVersion;
      if (taskParams().validateOnlyMasterLeader) {
        sourceYbcVersion =
            ybcClientService.getYbcServerVersion(
                universe.getMasterLeaderHostText(),
                universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort,
                universe.getCertificateNodetoNode());
      } else {
        sourceYbcVersion = ybcUpgrade.getUniverseYbcVersion(universe.universeUUID);
      }
      if (!sourceYbcVersion.equals(taskParams().ybcVersion)) {
        throw new RuntimeException(
            "Error occurred while upgrading ybc version "
                + taskParams().ybcVersion
                + " on universe "
                + taskParams().universeUUID);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    } finally {
      ybcUpgrade.removeYBCUpgradeProcess(taskParams().universeUUID);
    }
  }
}
