package com.yugabyte.yw.commissioner.tasks.subtasks;

import javax.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeManager;

import lombok.extern.slf4j.Slf4j;

import com.yugabyte.yw.commissioner.tasks.params.NodeAccessTaskParams;

@Slf4j
public class VerifyNodeSSHAccess extends NodeTaskBase {

  @Inject
  protected VerifyNodeSSHAccess(
      BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  @Override
  protected NodeAccessTaskParams taskParams() {
    return (NodeAccessTaskParams) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Verify_Node_SSH_Access, taskParams())
        .processErrors();
  }
}
