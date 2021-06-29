/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;

import javax.inject.Inject;
import java.util.Set;

@Slf4j
public class CheckTServers extends UniverseDefinitionTaskBase {

  @Inject
  protected CheckTServers(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
      // Get the list of tservers.
      Set<NodeDetails> tserverNodes =
          taskParams().getNodesInCluster(taskParams().getPrimaryCluster().uuid);
      // Wait for tservers to be responsive.
      createWaitForServersTasks(tserverNodes, ServerType.TSERVER);
      // Run the task.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    }
  }
}
