// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import javax.inject.Inject;

public class WaitForNodeAgent extends NodeTaskBase {
  private final NodeAgentClient nodeAgentClient;

  @Inject
  protected WaitForNodeAgent(
      BaseTaskDependencies baseTaskDependencies, NodeAgentClient nodeAgentClient) {
    super(baseTaskDependencies);
    this.nodeAgentClient = nodeAgentClient;
  }

  public static class Params extends NodeTaskParams {
    public Duration timeout;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNode(taskParams().nodeName);
    NodeAgent.maybeGetByIp(node.cloudInfo.private_ip)
        .ifPresent(
            n -> {
              nodeAgentClient.waitForServerReady(n, taskParams().timeout);
              n.saveState(State.READY);
            });
  }
}
