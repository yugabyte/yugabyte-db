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
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import java.time.Duration;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AnsibleClusterServerCtl extends NodeTaskBase {

  @Inject
  protected AnsibleClusterServerCtl(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public String process;
    public String command;
    public int sleepAfterCmdMills = 0;
    public boolean isIgnoreError = false;

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
      if (ServerType.MASTER.name().equalsIgnoreCase(taskParams().process)
          && "start".equalsIgnoreCase(taskParams().command)) {
        // Master is fully configured and ready to start.
        // TODO This is not the right place but this comes after AnsibleConfigureServer
        // which does too many things.
        setNodeStatus(NodeStatus.builder().masterState(MasterState.Configured).build());
      }
      NodeDetails nodeDetails = null;
      Optional<Universe> universeOpt = Universe.maybeGet(taskParams().getUniverseUUID());
      if (!universeOpt.isPresent()
          || (nodeDetails = universeOpt.get().getNode(taskParams().nodeName)) == null) {
        log.warn(
            "Universe or node {} does not exist. Skipping server control command - {}",
            taskParams().nodeName,
            taskParams().command);
        return;
      }
      if (nodeDetails.isSoftwareDeleted()) {
        // This is to ensure that the command is not issued after the software is uninstalled.
        // This applies mostly for stop process on retry.
        log.warn(
            "Software is already removed from {}. Skipping stop {}",
            taskParams().nodeName,
            taskParams().command);
        return;
      }
      // Execute the ansible command.
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Control, taskParams())
          .processErrors();
    } catch (Exception e) {
      if (!taskParams().isIgnoreError) {
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
