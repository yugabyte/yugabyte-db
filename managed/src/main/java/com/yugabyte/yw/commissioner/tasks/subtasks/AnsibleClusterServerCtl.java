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
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AnsibleClusterServerCtl extends NodeTaskBase {

  @Inject
  protected AnsibleClusterServerCtl(
      BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  public static class Params extends NodeTaskParams {
    public String process;
    public String command;
    public int sleepAfterCmdMills = 0;
    public boolean isForceDelete = false;

    // Systemd vs Cron Option (Default: Cron)
    public boolean useSystemd = false;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().nodeName
        + ", "
        + taskParams().process
        + ": "
        + taskParams().command
        + ")";
  }

  @Override
  public void run() {
    try {
      // Execute the ansible command.
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      taskParams().useSystemd =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;
      ShellResponse response =
          getNodeManager().nodeCommand(NodeManager.NodeCommandType.Control, taskParams());
      processShellResponse(response);
    } catch (Exception e) {
      if (!taskParams().isForceDelete) {
        throw e;
      } else {
        log.debug("Ignoring error: {}", e.getMessage());
      }
    }

    if (taskParams().sleepAfterCmdMills > 0) {
      try {
        Thread.sleep(getSleepMultiplier() * taskParams().sleepAfterCmdMills);
      } catch (InterruptedException e) {
        log.error("{} Thread Sleep failed: {}", getName(), e.getMessage());
      }
    }
  }
}
