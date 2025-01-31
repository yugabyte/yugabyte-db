// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.NodeInstance;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RecommissionNodeInstance extends AbstractTaskBase {

  @Inject
  protected RecommissionNodeInstance(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected DetachedNodeTaskParams taskParams() {
    return (DetachedNodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    NodeInstance nodeInstance = NodeInstance.getOrBadRequest(taskParams().getNodeUuid());

    if (nodeInstance.getState() != NodeInstance.State.DECOMMISSIONED) {
      throw new RuntimeException(
          String.format(
              "Node instance %s in %s state cannot be recommissioned. Node instance must be in %s"
                  + " state to be recommissioned.",
              nodeInstance.getNodeUuid(),
              nodeInstance.getState(),
              NodeInstance.State.DECOMMISSIONED));
    }

    if (!nodeInstance.isManuallyDecommissioned()) {
      log.debug("Cleaning up node instance {}", nodeInstance.getNodeUuid());
      try {
        ShellResponse response =
            nodeManager
                .detachedNodeCommand(NodeManager.NodeCommandType.Destroy, taskParams())
                .processErrors();
      } catch (Exception e) {
        log.error("Clean up failed for node instance: {}", nodeInstance.getNodeUuid(), e);
        throw e;
      }
      log.debug("Successfully cleaned up node instance: {}", nodeInstance.getNodeUuid());
    } else {
      log.debug(
          "Skipping clean up node instance {} as node instance was manually decommissioned by user",
          nodeInstance.getNodeUuid());
    }

    nodeInstance.clearNodeDetails();
  }
}
