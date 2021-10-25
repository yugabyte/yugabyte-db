// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import javax.inject.Inject;
import play.libs.Json;

public class PrecheckNodeDetached extends AbstractTaskBase {

  private NodeManager nodeManager;

  @Inject
  protected PrecheckNodeDetached(
      BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies);
    this.nodeManager = nodeManager;
  }

  public NodeManager getNodeManager() {
    return nodeManager;
  }

  @Override
  protected DetachedNodeTaskParams taskParams() {
    return (DetachedNodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    ShellResponse response =
        getNodeManager().detachedNodeCommand(NodeManager.NodeCommandType.Precheck, taskParams());

    if (response.code == 0) {
      JsonNode responseJson = Json.parse(response.message);
      for (JsonNode node : responseJson) {
        if (!node.isBoolean() || !node.asBoolean()) {
          // If a check failed, change the return code so processShellResponse errors.
          response.code = 1;
          break;
        }
      }
    }
    processShellResponse(response);
  }
}
