// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.NodeInstance.State;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DecommissionNodeInstance extends AbstractTaskBase {

  @Inject
  protected DecommissionNodeInstance(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected DetachedNodeTaskParams taskParams() {
    return (DetachedNodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    NodeInstance nodeInstance = NodeInstance.getOrBadRequest(taskParams().getNodeUuid());

    if (nodeInstance.getState() != NodeInstance.State.FREE) {
      throw new RuntimeException(
          String.format(
              "Node instance %s in %s state cannot be manually decommissioned. Node instance must"
                  + " be in %s state to be recommissioned.",
              nodeInstance.getNodeUuid(), nodeInstance.getState(), NodeInstance.State.FREE));
    }

    nodeInstance.setState(State.DECOMMISSIONED);
    nodeInstance.setManuallyDecommissioned(true);
    nodeInstance.update();
    log.debug(
        "Successfully set node instance {} to {} state",
        nodeInstance.getNodeUuid(),
        State.DECOMMISSIONED);
  }
}
