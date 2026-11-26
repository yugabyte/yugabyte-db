// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.NodeAgentPoller;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.DeployContext;
import com.yugabyte.yw.models.NodeAgent.DeployType;
import com.yugabyte.yw.models.NodeAgent.State;
import java.util.UUID;
import javax.inject.Inject;

public class RunUpgradeNodeAgent extends NodeTaskBase {
  private final NodeAgentPoller nodeAgentPoller;

  @Inject
  protected RunUpgradeNodeAgent(
      BaseTaskDependencies baseTaskDependencies, NodeAgentPoller nodeAgentPoller) {
    super(baseTaskDependencies);
    this.nodeAgentPoller = nodeAgentPoller;
  }

  public static class Params extends NodeTaskParams {
    public String nodeIp;
    public UUID certificateUuid;
    public boolean certsOnly;
  }

  public Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    NodeAgent nodeAgent = NodeAgent.maybeGetByIp(taskParams().nodeIp).orElseThrow();
    nodeAgentPoller.upgradeNodeAgent(
        nodeAgent.getUuid(),
        false /* waitForInFlightUpgrade */,
        n -> {
          if (n.getState() == State.UPGRADED) {
            // In this state, the context is not applied. Throw exception to avoid silently ignoring
            // the context.
            throw new IllegalStateException(
                String.format(
                    "Upgrade is not allowed in UPGRADED state for node agent %s. Wait for the state"
                        + " to become READY before running the upgrade again.",
                    n));
          }
          DeployType deployType = DeployType.FULL;
          if (taskParams().certsOnly && nodeAgentPoller.versionMatched(n)) {
            deployType = DeployType.CERTS_ONLY;
          }
          return DeployContext.builder()
              .certificateUuid(taskParams().certificateUuid)
              .deployType(deployType)
              .build();
        });
  }
}
