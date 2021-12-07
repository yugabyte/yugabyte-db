// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ChangeInstanceType extends NodeTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(PauseServer.class);

  @Inject
  protected ChangeInstanceType(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  public static class Params extends NodeTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().instanceType + ")";
  }

  @Override
  public void run() {
    LOG.info(
        "Running ChangeInstanceType against node {} to change its type from {} to {}",
        taskParams().nodeName,
        Universe.getOrBadRequest(taskParams().universeUUID)
            .getNode(taskParams().nodeName)
            .cloudInfo
            .instance_type,
        taskParams().instanceType);

    ShellResponse response =
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Change_Instance_Type, taskParams());
    processShellResponse(response);
  }
}
