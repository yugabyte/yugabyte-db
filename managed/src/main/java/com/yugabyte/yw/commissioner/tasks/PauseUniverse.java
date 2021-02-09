/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import java.util.HashSet;
import java.util.Set;
import java.util.UUID;

public class PauseUniverse extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(PauseUniverse.class);

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

      Universe universe = null;
      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag
      // to prevent other updates from happening.
      universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */);
      universe = Universe.get(params().universeUUID);

      Set<NodeDetails> tserverNodes = new HashSet<NodeDetails>(universe.getTServers());
      Set<NodeDetails> masterNodes = new HashSet<NodeDetails>(universe.getMasters());
      
      for (NodeDetails node : tserverNodes) {
        createTServerTaskForNode(node, "stop").setSubTaskGroupType(
            SubTaskGroupType.StoppingNodeProcesses);
      }
      createStopMasterTasks(masterNodes).setSubTaskGroupType(
          SubTaskGroupType.StoppingNodeProcesses);
          
      if (!universe.getUniverseDetails().isImportedUniverse()) {
        // Create tasks to pause the existing nodes.
        createPauseServerTasks(universe.getNodes()).setSubTaskGroupType(
            SubTaskGroupType.PauseUniverse);
      }
      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.PauseUniverse);
      // Run all the tasks.
      subTaskGroupQueue.run();

      Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
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
      try {
        // If for any reason pause universe fails we would just unlock the universe for update
        unlockUniverseForUpdate();
      } catch (Throwable t1) {
        // Ignore the error
      }

      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    }
    LOG.info("Finished {} task.", getName());
  }
}
