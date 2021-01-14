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

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
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

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = null;
      universe = Universe.get(params().universeUUID);

      if (!universe.getUniverseDetails().isImportedUniverse()) {
        // Create tasks to resume the existing nodes.
        createResumeServerTasks(
          universe.getNodes()
        ).setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);
      }

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      // If for any reason resume universe fails we would just unlock the universe for update
      try {
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
