// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeManager;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class HardRebootServer extends NodeTaskBase {

  @Inject
  protected HardRebootServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Hard rebooting instance {}", taskParams().nodeName);
    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Hard_Reboot, taskParams())
        .processErrors();
  }
}
