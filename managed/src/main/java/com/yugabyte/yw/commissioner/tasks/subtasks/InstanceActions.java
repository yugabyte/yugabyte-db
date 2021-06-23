/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import lombok.extern.slf4j.Slf4j;

import javax.inject.Inject;

@Slf4j
public class InstanceActions extends NodeTaskBase {

  @Inject
  protected InstanceActions(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    public NodeManager.NodeCommandType type;
    // CSV of tag keys to be deleted.
    public String deleteTags = "";
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info(
        "Running Instance action {} type {} against node {}",
        getName(),
        taskParams().type.toString(),
        taskParams().nodeName);

    ShellResponse response = getNodeManager().nodeCommand(taskParams().type, taskParams());
    processShellResponse(response);
  }
}
