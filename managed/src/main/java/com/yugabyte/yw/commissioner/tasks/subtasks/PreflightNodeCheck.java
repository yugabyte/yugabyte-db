// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.NodeInstance;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class PreflightNodeCheck extends NodeTaskBase {

  @Inject
  protected PreflightNodeCheck(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
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
    Params taskParams = taskParams();
    log.info("Running preflight checks for node {}.", taskParams.nodeName);
    ShellResponse response =
        getNodeManager().nodeCommand(NodeManager.NodeCommandType.Precheck, taskParams);
    if (response.code == 0) {
      JsonNode responseJson = Json.parse(response.message);
      for (JsonNode nodeContent : responseJson) {
        if (!nodeContent.isBoolean() || !nodeContent.asBoolean()) {
          String errString =
              "Failed preflight checks for node " + taskParams.nodeName + ":\n" + response.message;
          log.error(errString);
          if (!taskParams.reserveNodes) {
            NodeInstance node = NodeInstance.getByName(taskParams.nodeName);
            node.clearNodeDetails();
          }
          throw new RuntimeException(errString);
        }
      }
    }
    // TODO Non-zero is not handled in the existing pre-flight check.
  }
}
