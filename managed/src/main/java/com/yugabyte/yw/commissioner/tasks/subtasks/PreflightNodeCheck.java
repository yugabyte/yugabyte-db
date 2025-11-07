// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.NodeAgentEnabler;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeConfigValidator;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PreflightNodeCheck extends NodeTaskBase {

  private final NodeConfigValidator nodeConfigValidator;

  @Inject
  protected PreflightNodeCheck(
      BaseTaskDependencies baseTaskDependencies, NodeConfigValidator nodeConfigValidator) {
    super(baseTaskDependencies);
    this.nodeConfigValidator = nodeConfigValidator;
  }

  // Parameters for precheck task.
  public static class Params extends NodeTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running preflight checks for node {}.", taskParams().nodeName);
    Provider provider = taskParams().getProvider();
    if (provider.getCloudCode() == CloudType.onprem && provider.getDetails().skipProvisioning) {
      Universe universe = getUniverse();
      if (getInstanceOf(NodeAgentEnabler.class).isNodeAgentServerEnabled(provider, universe)) {
        log.info("Checking for presence of node agent in YBA for node {}", taskParams().nodeName);
        // Ensure that node agent is already installed for a new manual onprem-provider when node
        // agent client is enabled.
        NodeInstance instance = NodeInstance.getOrBadRequest(taskParams().nodeUuid);
        Optional<NodeAgent> nodeAgentOpt = NodeAgent.maybeGetByIp(instance.getDetails().ip);
        if (nodeAgentOpt.isEmpty() || !nodeAgentOpt.get().isActive()) {
          String errMsg =
              String.format(
                  "Node agent is expected for %s but it is not installed or inactive",
                  instance.getDetails().ip);
          log.error(errMsg);
          throw new IllegalStateException(errMsg);
        }
      }
    }
    ShellResponse response =
        getNodeManager().nodeCommand(NodeManager.NodeCommandType.Precheck, taskParams());
    try {
      PrecheckNodeDetached.processPreflightResponse(
          nodeConfigValidator,
          taskParams().getProvider(),
          taskParams().nodeUuid,
          taskParams().getNodeName(),
          false,
          response);
    } catch (RuntimeException e) {
      log.error(
          "Failed preflight checks for node {}:\n{}", taskParams().nodeName, response.message);
      throw e;
    }
  }
}
