// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.NodeAgentPoller;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.DeployContext;
import com.yugabyte.yw.models.NodeAgent.DeployType;
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
    DeployType deployType = DeployType.FULL;
    if (taskParams().certsOnly && nodeAgentPoller.versionMatched(nodeAgent)) {
      deployType = DeployType.CERTS_ONLY;
    }
    DeployContext deployContext =
        DeployContext.builder()
            .certificateUuid(taskParams().certificateUuid)
            .deployType(deployType)
            .build();
    nodeAgentPoller.upgradeNodeAgent(
        nodeAgent.getUuid(), false /* waitForInFlightUpgrade */, n -> deployContext);
  }
}
