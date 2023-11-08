// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.helpers.NodeConfigValidator;
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
  public static class Params extends NodeTaskParams {
    // Whether nodes should remain reserved or not.
    public boolean reserveNodes = true;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running preflight checks for node {}.", taskParams().nodeName);
    ShellResponse response =
        getNodeManager().nodeCommand(NodeManager.NodeCommandType.Precheck, taskParams());
    try {
      PrecheckNodeDetached.processPreflightResponse(
          nodeConfigValidator, taskParams().getProvider(), taskParams().nodeUuid, false, response);
    } catch (RuntimeException e) {
      log.error(
          "Failed preflight checks for node {}:\n{}", taskParams().nodeName, response.message);
      // TODO this may not be applicable now.
      if (!taskParams().reserveNodes) {
        NodeInstance node = NodeInstance.getByName(taskParams().nodeName);
        node.clearNodeDetails();
      }
      throw e;
    }
  }
}
