/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.base.Stopwatch;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import java.time.Duration;
import java.util.Collections;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.YBClient;

@Slf4j
public class AnsibleClusterServerCtl extends NodeTaskBase {
  private final NodeAgentRpcPayload nodeAgentRpcPayload;
  private final YbClientConfigFactory ybClientConfigFactory;

  @Inject
  protected AnsibleClusterServerCtl(
      BaseTaskDependencies baseTaskDependencies,
      NodeAgentRpcPayload nodeAgentRpcPayload,
      YbClientConfigFactory ybClientConfigFactory) {
    super(baseTaskDependencies);
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
    this.ybClientConfigFactory = ybClientConfigFactory;
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
    // Make best effort to flush tablets before stopping tserver.
    public boolean flushTabletsOnStopTserver = false;
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
      if (taskParams().flushTabletsOnStopTserver
          && ServerType.TSERVER.name().equalsIgnoreCase(taskParams().process)
          && "stop".equalsIgnoreCase(taskParams().command)) {
        // Flush tablets before stopping tserver.
        flushTablets(universeOpt.get(), nodeDetails);
      }
      boolean isNodeAgentSupported =
          NodeAgentClient.isCloudTypeSupported(
              universeOpt.get().getUniverseDetails().getPrimaryCluster().userIntent.providerType);
      if (isNodeAgentSupported) {
        NodeAgent nodeAgent =
            nodeAgentClient.getAndUpgradeOrThrow(nodeDetails.cloudInfo.private_ip);
        runWithNodeAgent(nodeAgent, universeOpt.get(), nodeDetails);
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

  // Best effort to flush tablets before stopping tserver with a timeout.
  // If it fails, we will just log and continue stopping tserver.
  private void flushTablets(Universe universe, NodeDetails tserverNode) {
    Duration flushTimeout =
        confGetter.getConfForScope(universe, UniverseConfKeys.flushTabletsTimeoutOnStopTserver);
    if (flushTimeout.isZero() || flushTimeout.isNegative()) {
      log.warn(
          "Flush tablets timeout is not set or invalid for universe {}. Skipping flush tablets.",
          universe.getUniverseUUID());
      return;
    }
    log.debug(
        "Flushing tablets before stopping tserver {}:{} with timeout {} seconds",
        tserverNode.nodeName,
        tserverNode.tserverRpcPort,
        flushTimeout);
    YbClientConfig config =
        ybClientConfigFactory.create(
            universe.getMasterAddresses(), universe.getCertificateNodetoNode());
    config.setOperationTimeout(flushTimeout);
    config.setAdminOperationTimeout(flushTimeout);
    Stopwatch flushStopwatch = Stopwatch.createStarted();
    try (YBClient ybClient = ybService.getClientWithConfig(config)) {
      ybClient.flushTablets(
          tserverNode.cloudInfo.private_ip, tserverNode.tserverRpcPort, Collections.emptyList());
    } catch (Exception e) {
      log.warn(
          "Error while flushing tablets for universe {}: {}. Continuing with the error: {}",
          universe.getUniverseUUID(),
          e.getMessage());
    } finally {
      log.debug(
          "Finished flushing tablets for tserver {}:{} in {}ms",
          tserverNode.nodeName,
          tserverNode.tserverRpcPort,
          flushStopwatch.elapsed().toMillis());
    }
  }
}
