// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.YbcUpgrade;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.time.Duration;
import java.util.List;
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
    @Deprecated public boolean validateOnlyMasterLeader = false;
    public boolean validateOnlyLiveNodes = false;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  private void preChecks(Universe universe, String ybcVersion) {
    if (taskParams().validateOnlyMasterLeader) {
      log.warn(
          "`validateOnlyMasterLeader` is deprecated, setting `validateOnlyLiveNodes` to true.");
      taskParams().validateOnlyLiveNodes = true;
    }

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

      if (!success && !taskParams().validateOnlyLiveNodes) {
        throw new RuntimeException("YBC Upgrade task did not complete in expected time.");
      }

      List<String> sourceYbcVersions =
          ybcUpgrade.getUniverseNodeYbcVersions(universe, taskParams().validateOnlyLiveNodes);

      if (sourceYbcVersions.stream()
          .filter(v -> !v.equals(taskParams().ybcVersion))
          .findAny()
          .isPresent()) {
        throw new RuntimeException(
            "Error occurred while upgrading ybc version "
                + taskParams().ybcVersion
                + " on universe "
                + taskParams().universeUUID);
      }
      // Only update the universe YBC version if all nodes have the new version.
      if (sourceYbcVersions.size() == universe.getNodes().size()) {
        UniverseUpdater updater =
            new UniverseUpdater() {
              @Override
              public void run(Universe universe) {
                UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
                universeDetails.setYbcSoftwareVersion(taskParams().ybcVersion);
                universe.setUniverseDetails(universeDetails);
              }
            };
        Universe.saveDetails(universe.getUniverseUUID(), updater, false /* increment version */);
      } else {
        log.warn(
            "Not all nodes have been upgraded to YBC version {} on universe {}",
            taskParams().ybcVersion,
            taskParams().universeUUID);
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
