/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;

import javax.inject.Inject;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;

@Slf4j
public class PauseUniverse extends UniverseTaskBase {

  @Inject
  protected PauseUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */);

      Set<NodeDetails> tserverNodes = new HashSet<>(universe.getTServers());
      Set<NodeDetails> masterNodes = new HashSet<>(universe.getMasters());

      for (NodeDetails node : tserverNodes) {
        createTServerTaskForNode(node, "stop")
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }
      createStopMasterTasks(masterNodes)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      if (!universe.getUniverseDetails().isImportedUniverse()) {
        // Create tasks to pause the existing nodes.
        createPauseServerTasks(universe.getNodes())
            .setSubTaskGroupType(SubTaskGroupType.PauseUniverse);
      }
      createSwamperTargetUpdateTask(false);
      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.PauseUniverse);
      // Run all the tasks.
      subTaskGroupQueue.run();

      Universe.UniverseUpdater updater =
          new Universe.UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.universePaused = true;
              universe.setUniverseDetails(universeDetails);
            }
          };
      saveUniverseDetails(updater);

      unlockUniverseForUpdate();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      // Run an unlock in case the task failed before getting to the unlock. It is okay if it
      // errors out.
      unlockUniverseForUpdate();
      throw t;
    }
    log.info("Finished {} task.", getName());
  }
}
