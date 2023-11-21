/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PauseServer extends NodeTaskBase {
  @Inject
  protected PauseServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    // IP of node to be paused.
    public String nodeIP = null;
  }

  @Override
  protected PauseServer.Params taskParams() {
    return (PauseServer.Params) taskParams;
  }

  private void pauseUniverse(final String nodeName) {
    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (u.getNode(nodeName) == null) {
      log.error("No node in universe with name " + nodeName);
      return;
    }
    log.info("Paused the node " + nodeName + " from universe " + taskParams().getUniverseUUID());
  }

  @Override
  public void run() {
    try {
      // Update the node state as stopping also can not set the node state to stopped
      // as it will be not reachable.
      setNodeState(NodeDetails.NodeState.Stopping);
      getNodeManager().nodeCommand(NodeManager.NodeCommandType.Pause, taskParams()).processErrors();
      pauseUniverse(taskParams().nodeName);
      setNodeState(NodeDetails.NodeState.Stopped);
    } catch (Exception e) {
      throw e;
    }
  }
}
