// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpgradeUniverseYbc extends UniverseTaskBase {

  @Inject private ReleaseManager releaseManager;

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

      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

      if (!universeDetails.enableYbc || !universeDetails.ybcInstalled) {
        throw new RuntimeException(
            "Ybc is either not installed or enabled on the universe: " + universe.universeUUID);
      }

      // Check whether the target ybc version is present in YB-Anywhere.
      if (releaseManager.getYbcReleaseByVersion(taskParams().ybcSoftwareVersion) == null) {
        throw new RuntimeException(
            "Target ybc package " + taskParams().ybcSoftwareVersion + " does not exists.");
      }

      // create task for upgrading ybc version on universe.
      createUpgradeYbcTask(
              taskParams().universeUUID,
              taskParams().ybcSoftwareVersion,
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
