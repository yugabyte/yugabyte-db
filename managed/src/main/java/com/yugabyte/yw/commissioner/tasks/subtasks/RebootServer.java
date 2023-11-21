// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RebootServer extends NodeTaskBase {

  @Inject
  protected RebootServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public boolean useSSH = true;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Rebooting instance {}", taskParams().nodeName);
    getNodeManager().nodeCommand(NodeManager.NodeCommandType.Reboot, taskParams()).processErrors();
  }
}
