// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class InstanceExistCheck extends NodeTaskBase {

  @Inject
  protected InstanceExistCheck(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    if (instanceExists(taskParams())) {
      log.info("Waiting for SSH to succeed on existing instance {}", taskParams().nodeName);
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Wait_For_SSH, taskParams())
          .processErrors();
    }
  }
}
