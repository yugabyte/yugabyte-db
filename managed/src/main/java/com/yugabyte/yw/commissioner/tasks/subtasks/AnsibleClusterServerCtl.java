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
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import java.time.Duration;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class AnsibleClusterServerCtl extends NodeTaskBase {
  private final NodeAgentRpcPayload nodeAgentRpcPayload;

  @Inject
  protected AnsibleClusterServerCtl(
      BaseTaskDependencies baseTaskDependencies, NodeAgentRpcPayload nodeAgentRpcPayload) {
    super(baseTaskDependencies);
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
  }

  public static class Params extends NodeTaskParams {
    public String process;
    public String command;
    public int sleepAfterCmdMills = 0;
    public boolean isIgnoreError = false;
    public boolean checkVolumesAttached = false;
    // Set it to deconfigure the server like deleting the conf file.
    public boolean deconfigure = false;
    // Skip stopping processes if VM is paused.
    public boolean skipStopForPausedVM = false;
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
      if (nodeDetails.cloudInfo == null || StringUtils.isEmpty(nodeDetails.cloudInfo.private_ip)) {
        log.warn(
            "Node {} has no IP in the universe {}. Skipping server control command - {}",
            taskParams().nodeName,
            universeOpt.get().getUniverseUUID(),
            taskParams().command);
        return;
      }
      if (ServerType.MASTER.name().equalsIgnoreCase(taskParams().process)
          && "start".equalsIgnoreCase(taskParams().command)
          && nodeDetails.masterState != null) {
        // Master is fully configured and ready to start. Set this only if masterState was
        // previously set. Some tasks may just start/stop master without changing master state.
        // TODO This is not the right place but this comes after AnsibleConfigureServer
        // which does too many things.
        setNodeStatus(NodeStatus.builder().masterState(MasterState.Configured).build());
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
      Optional<NodeAgent> optional =
          confGetter.getGlobalConf(GlobalConfKeys.nodeAgentDisableConfigureServer)
              ? Optional.empty()
              : nodeUniverseManager.maybeGetNodeAgent(
                  universeOpt.get(), nodeDetails, true /*check feature flag*/);
      if (optional.isPresent()) {
        runWithNodeAgent(optional.get(), universeOpt.get(), nodeDetails);
      } else {
        runLegacy(universeOpt.get(), nodeDetails);
      }
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

  public void runLegacy(Universe universe, NodeDetails nodeDetails) {
    // Execute the ansible command.
    getNodeManager().nodeCommand(NodeManager.NodeCommandType.Control, taskParams()).processErrors();
  }

  public void runWithNodeAgent(NodeAgent nodeAgent, Universe universe, NodeDetails nodeDetails) {
    // These conditional checks are in python layer for legacy path.
    if (!taskParams().skipStopForPausedVM || isInstanceRunning(taskParams())) {
      nodeAgentClient.runServerControl(
          nodeAgent,
          nodeAgentRpcPayload.setupServerControlBits(universe, nodeDetails, taskParams()),
          NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
    }
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
