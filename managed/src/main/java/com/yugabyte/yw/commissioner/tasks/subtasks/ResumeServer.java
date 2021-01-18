/*
 * Copyright 2019 YugaByte, Inc. and Contributors
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
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class ResumeServer extends NodeTaskBase {

  public static class Params extends NodeTaskParams {
    // IP of node to be deleted.
    public String nodeIP = null;
  }

  @Override
  protected ResumeServer.Params taskParams() {
    return (ResumeServer.Params)taskParams;
  }

  public static final Logger LOG = LoggerFactory.getLogger(ResumeServer.class);

  private void resumeUniverse(final String nodeName) {
    Universe u = Universe.get(taskParams().universeUUID);
    if (u.getNode(nodeName) == null) {
      LOG.error("No node in universe with name " + nodeName);
      return;
    }
    // Persist the desired node information into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        LOG.debug("Resuming node " + nodeName + " from universe " + taskParams().universeUUID);
      }
    };
  }

  @Override
  public void run() {
    // Update the node state as resuming..
    // setNodeState(NodeDetails.NodeState.Starting);
    // Execute the ansible command.
    try {
      ShellResponse response = getNodeManager().nodeCommand(NodeManager.NodeCommandType.Resume, taskParams());
      processShellResponse(response);
      setNodeState(NodeDetails.NodeState.Live);
      resumeUniverse(taskParams().nodeName);
    } catch (Exception e) {
      throw e;
    }

  }
}
