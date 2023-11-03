// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpgradeUniverseYbc extends UniverseTaskBase {

  @Inject private ReleaseManager releaseManager;
  @Inject private YbcManager ybcManager;

  @Inject
  protected UpgradeUniverseYbc(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening. Does not alter 'updateSucceeded' flag so as not
      // to lock out the universe completely in case this task fails.
      lockUniverse(-1 /* expectedUniverseVersion */);

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

      if (!universeDetails.isEnableYbc() || !universeDetails.isYbcInstalled()) {
        throw new RuntimeException(
            "Ybc is either not installed or enabled on the universe: "
                + universe.getUniverseUUID());
      }

      // Check whether the target ybc version is present in YB-Anywhere for each node.
      universe
          .getNodes()
          .forEach(
              (node) -> {
                Pair<String, String> ybcPackageDetails =
                    ybcManager.getYbcPackageDetailsForNode(universe, node);
                if (releaseManager.getYbcReleaseByVersion(
                        taskParams().getYbcSoftwareVersion(),
                        ybcPackageDetails.getFirst(),
                        ybcPackageDetails.getSecond())
                    == null) {
                  throw new RuntimeException(
                      "Target ybc package "
                          + taskParams().getYbcSoftwareVersion()
                          + " does not exists for node"
                          + node.nodeName);
                }
              });

      // create task for upgrading ybc version on universe.
      createUpgradeYbcTask(
              taskParams().getUniverseUUID(),
              taskParams().getYbcSoftwareVersion(),
              false /* ValidateOnlyMasterLeader */)
          .setSubTaskGroupType(SubTaskGroupType.UpgradingYbc);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
