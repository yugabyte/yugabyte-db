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
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckMasters extends UniverseDefinitionTaskBase {

  // The RPC timeouts.
  protected static final Duration RPC_TIMEOUT_MS = Duration.ofMillis(5000L);

  @Inject
  protected CheckMasters(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    try {
      // Get the list of masters.
      // Note that at this point, we have only added the masters into the cluster.
      Set<NodeDetails> masterNodes =
          taskParams().getNodesInCluster(taskParams().getPrimaryCluster().uuid);
      // Wait for new masters to be responsive.
      createWaitForServersTasks(masterNodes, ServerType.MASTER, RPC_TIMEOUT_MS);
      // Run the task.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    }
  }
}
