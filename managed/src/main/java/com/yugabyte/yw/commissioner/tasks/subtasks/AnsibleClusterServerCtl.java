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
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
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

    public boolean checkVolumesAttached = false;
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
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Control, taskParams())
          .processErrors();
    } catch (Exception e) {
      if (!taskParams().isForceDelete) {
        throw e;
      } else {
        log.debug("Ignoring error: {}", e.getMessage());
      }
    }

    if (taskParams().sleepAfterCmdMills > 0) {
      waitFor(Duration.ofMillis((long) getSleepMultiplier() * taskParams().sleepAfterCmdMills));
    }
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
