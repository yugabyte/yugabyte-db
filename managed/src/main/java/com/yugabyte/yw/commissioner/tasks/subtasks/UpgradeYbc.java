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
import lombok.extern.slf4j.Slf4j;

@Slf4j
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
    if (!universe.getUniverseDetails().isEnableYbc()) {
      throw new RuntimeException(
          "Cannot upgrade YBC as it is not enabled on universe " + universe.getUniverseUUID());
    }
    if (universe.getUniverseDetails().getYbcSoftwareVersion().equals(ybcVersion)) {
      throw new RuntimeException(
          "YBC version "
              + ybcVersion
              + " is already installed on universe "
              + universe.getUniverseUUID());
    }
  }

  @Override
  public void run() {
    try {
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      preChecks(universe, taskParams().ybcVersion);
      ybcUpgrade.upgradeYBC(taskParams().universeUUID, taskParams().ybcVersion, true /* force */);
      waitForYbcUpgrade();
      boolean success =
          ybcUpgrade.pollUpgradeTaskResult(
              taskParams().universeUUID, taskParams().ybcVersion, true /* verbose */);
      log.info(
          "Polling ybc upgrade for the universe {} resulted in the state {}",
          taskParams().universeUUID,
          success);

      if (!success && !taskParams().validateOnlyMasterLeader) {
        throw new RuntimeException("YBC Upgrade task did not complete in expected time.");
      }

      // Even if the ybc upgrade fails for the universe, we will validate the ybc version
      // on master leader separately.
      String sourceYbcVersion;
      if (taskParams().validateOnlyMasterLeader) {
        // Fetch ybc version from master leader node.
        sourceYbcVersion =
            ybcClientService.getYbcServerVersion(
                universe.getMasterLeaderHostText(),
                universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort,
                universe.getCertificateNodetoNode());
      } else {
        // Fetch ybc version from universe details as we update ybc version as soon as ybc
        // is upgraded on each node during poll task result itself.
        sourceYbcVersion = ybcUpgrade.getUniverseYbcVersion(universe.getUniverseUUID());
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

  private int waitForYbcUpgrade() {
    int numRetries = 0;
    while (numRetries < ybcUpgrade.MAX_YBC_UPGRADE_POLL_RESULT_TRIES) {
      numRetries++;
      if (!ybcUpgrade.checkYBCUpgradeProcessExists(taskParams().universeUUID)) {
        break;
      } else if (ybcUpgrade.pollUpgradeTaskResult(
          taskParams().universeUUID, taskParams().ybcVersion, false /* verbose */)) {
        break;
      }
      waitFor(Duration.ofMillis(ybcUpgrade.YBC_UPGRADE_POLL_RESULT_SLEEP_MS));
    }
    return numRetries;
  }
}
