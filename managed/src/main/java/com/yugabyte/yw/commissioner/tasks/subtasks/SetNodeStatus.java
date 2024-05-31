// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.helpers.NodeStatus;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SetNodeStatus extends NodeTaskBase {

  @Inject
  protected SetNodeStatus(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public NodeStatus nodeStatus;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String toString() {
    return super.getName() + "(" + taskParams().nodeName + ", " + taskParams().nodeStatus + ")";
  }

  @Override
  public void run() {
    try {
      log.info(
          "Updating node {} status to {} in universe {}.",
          taskParams().nodeName,
          taskParams().nodeStatus,
          taskParams().getUniverseUUID());
      setNodeStatus(taskParams().nodeStatus);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
