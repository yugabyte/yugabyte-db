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

import com.yugabyte.yw.forms.UniverseTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import java.util.HashSet;
import java.util.Set;
import java.util.UUID;

public class ResumeUniverse extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ResumeUniverse.class);

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
  }

  public Params params() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
 
      Universe universe = null;
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      universe = lockUniverseForUpdate(-1 , true/* expectedUniverseVersion */);

      if (!universe.getUniverseDetails().isImportedUniverse()) {
        // Create tasks to resume the existing nodes.
        createResumeServerTasks(universe.getNodes()).setSubTaskGroupType(
            SubTaskGroupType.ResumeUniverse);
      }

      Set<NodeDetails> tserverNodes = new HashSet<NodeDetails>(universe.getTServers());
      Set<NodeDetails> masterNodes = new HashSet<NodeDetails>(universe.getMasters());

      for (NodeDetails node : tserverNodes) {
        createTServerTaskForNode(node, "start").setSubTaskGroupType(
            SubTaskGroupType.StartingNodeProcesses);
      }
      createStartMasterTasks(masterNodes).setSubTaskGroupType(
          SubTaskGroupType.StartingNodeProcesses);

      createWaitForServersTasks(tserverNodes, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      createWaitForServersTasks(masterNodes, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);
      // Run all the tasks.
      subTaskGroupQueue.run();
      unlockUniverseForUpdate();
    } catch (Throwable t) {
      try {
        // If for any reason resume universe fails we would just unlock the universe for update
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
