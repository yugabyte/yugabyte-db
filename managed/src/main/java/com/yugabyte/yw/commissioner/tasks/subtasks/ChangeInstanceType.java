// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ChangeInstanceType extends NodeTaskBase {

  @Inject
  protected ChangeInstanceType(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  public static class Params extends NodeTaskParams {
    public boolean force = false;
    // Amount of memory to limit the postgres process to via the ysql cgroup (in megabytes)
    public int cgroupSize = 0;
  }

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
    log.info(
        "Running ChangeInstanceType against node {} to change its type from {} to {}",
        taskParams().nodeName,
        Universe.getOrBadRequest(taskParams().getUniverseUUID())
            .getNode(taskParams().nodeName)
            .cloudInfo
            .instance_type,
        taskParams().instanceType);

    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Change_Instance_Type, taskParams())
        .processErrors();
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
