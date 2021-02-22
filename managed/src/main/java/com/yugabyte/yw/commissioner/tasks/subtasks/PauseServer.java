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

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class PauseServer extends NodeTaskBase {

  public static class Params extends NodeTaskParams {
    // IP of node to be paused.
    public String nodeIP = null;
  }

  @Override
  protected PauseServer.Params taskParams() {
    return (PauseServer.Params) taskParams;
  }

  public static final Logger LOG = LoggerFactory.getLogger(PauseServer.class);

  private void pauseUniverse(final String nodeName) {
    Universe u = Universe.get(taskParams().universeUUID);
    if (u.getNode(nodeName) == null) {
      LOG.error("No node in universe with name " + nodeName);
      return;
    }
    LOG.info("Paused the node " + nodeName + " from universe " + taskParams().universeUUID);
  }

  @Override
  public void run() {
    try {
      // Update the node state as stopping also can not set the node state to stopped
      // as it will be not reachable.
      setNodeState(NodeDetails.NodeState.Stopping);
      ShellResponse response = getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Pause, taskParams());
      processShellResponse(response);
      pauseUniverse(taskParams().nodeName);
      setNodeState(NodeDetails.NodeState.Stopped);
    } catch (Exception e) {
      throw e;
    }
  }
}
