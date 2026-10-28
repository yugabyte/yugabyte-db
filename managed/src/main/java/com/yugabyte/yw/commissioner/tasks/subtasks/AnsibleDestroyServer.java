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

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.nodeagent.DestroyServerInput;
import java.util.List;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class AnsibleDestroyServer extends NodeTaskBase {

  private static final int DESTROY_REMOTE_COMMAND_TIMEOUT_SECS = 300;

  @Inject
  protected AnsibleDestroyServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    // Flag to be set where errors from ansible will be ignored.
    public boolean isForceDelete;
    // Flag to track if node info should be deleted from universe db.
    public boolean deleteNode = true;
    // Flag to delete root volumes which are not auto-deleted on instance termination.
    public boolean deleteRootVolumes = false;
    // IP of node to be deleted.
    public String nodeIP = null;
    // Flag, indicating OpenTelemetry Collector is installed on the DB node.
    public boolean otelCollectorInstalled = false;
    // Skip changing node state after destroy if it is explicitly set.
    public boolean skipUpdateNodeState = false;
  }

  @Override
  protected AnsibleDestroyServer.Params taskParams() {
    return (AnsibleDestroyServer.Params) taskParams;
  }

  private void removeNodeFromUniverse(final String nodeName) {
    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (u.getNode(nodeName) == null) {
      log.warn("No node in universe with name {}", nodeName);
      return;
    }
    // Persist the desired node information into the DB.
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            log.debug(
                "Removing node {} from universe {}", nodeName, taskParams().getUniverseUUID());
            universe.getUniverseDetails().removeNode(nodeName);
          }
        };

    saveUniverseDetails(updater);
  }

  // DestroyServer cannot stop node-agent itself. Stop it over SSH before purge on non-manual
  // onprem while the node is still reachable.
  private void maybeUninstallNodeAgent(Universe universe, NodeDetails nodeDetails) {
    if (nodeDetails.cloudInfo == null || StringUtils.isEmpty(nodeDetails.cloudInfo.private_ip)) {
      return;
    }
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid);
    Provider provider = Util.getProviderForNode(nodeDetails, cluster);
    if (!provider.isNonManualOnprem()) {
      return;
    }
    Optional<NodeAgent> nodeAgentOpt = NodeAgent.maybeGetByIp(nodeDetails.cloudInfo.private_ip);
    if (nodeAgentOpt.isEmpty()) {
      return;
    }
    // Use SSH connection to stop node agent service, as the node agent cannot stop itself.
    ShellProcessContext shellContext =
        ShellProcessContext.builder()
            .useSshConnectionOnly(true)
            .timeoutSecs(DESTROY_REMOTE_COMMAND_TIMEOUT_SECS)
            .logCmdOutput(true)
            .build();
    String sshUser = imageBundleUtil.findEffectiveSshUser(provider, universe, nodeDetails);
    if (StringUtils.isNotEmpty(nodeDetails.sshUserOverride)) {
      sshUser = nodeDetails.sshUserOverride;
    }
    if (StringUtils.isNotEmpty(sshUser)) {
      shellContext = shellContext.toBuilder().sshUser(sshUser).build();
    }
    // For onprem non-manual, it is always root-systemd.
    StringBuilder cmdBuilder = new StringBuilder();
    cmdBuilder.append("sudo systemctl disable --now yb-node-agent.service && ");
    cmdBuilder.append("sudo rm -rf /etc/systemd/system/yb-node-agent.service && ");
    cmdBuilder.append("sudo systemctl daemon-reload && ");
    cmdBuilder.append("sudo rm -rf ").append("'").append(nodeAgentOpt.get().getHome()).append("'");
    String stopCmd = cmdBuilder.toString();
    List<String> command = ImmutableList.of("/bin/bash", "-c", stopCmd);
    log.info(
        "Stopping node agent service on node {} (IP {}) via SSH as user {}: {}",
        taskParams().nodeName,
        nodeDetails.cloudInfo.private_ip,
        shellContext.getSshUser(),
        stopCmd);
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(nodeDetails, universe, command, shellContext);
      if (response.isSuccess()) {
        log.info(
            "Successfully stopped node agent service on node {} (IP {}) via SSH as user {}",
            taskParams().nodeName,
            nodeDetails.cloudInfo.private_ip,
            shellContext.getSshUser());
      } else {
        log.warn(
            "Failed to stop node agent service on node {} (IP {}) via SSH as user {}: {}",
            taskParams().nodeName,
            nodeDetails.cloudInfo.private_ip,
            shellContext.getSshUser(),
            response.message);
      }
    } catch (Exception e) {
      // Best effort to stop node agent service, log the error and ignore error.
      log.warn(
          "Failed to stop node agent service on node {} (IP {}) via SSH as user {}: {}",
          taskParams().nodeName,
          nodeDetails.cloudInfo.private_ip,
          shellContext.getSshUser(),
          e.getMessage());
    }
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
    if (nodeDetails == null) {
      log.warn(
          "Node {} is not found in the universe {}",
          taskParams().nodeName,
          universe.getUniverseUUID());
      return;
    }
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid);
    CloudType cloudType = cluster.getProviderCloudType(nodeDetails);
    if (cloudType == Common.CloudType.onprem
        && (nodeDetails.cloudInfo == null
            || StringUtils.isEmpty(nodeDetails.cloudInfo.private_ip))) {
      // Node IP was never updated, nothing was changed. For onprem, it can just be cleared.
      // For CSPs, the instance needs to be terminated.
      log.warn(
          "Onprem node {} has no IP in the universe {}",
          taskParams().nodeName,
          universe.getUniverseUUID());
      NodeInstance.maybeGetByName(taskParams().nodeName, taskParams().nodeUuid)
          .ifPresent(n -> n.clearNodeDetails());
      return;
    }
    boolean cleanupFailed = true;
    try {
      Provider provider = Util.getProviderForNode(nodeDetails, cluster);
      boolean cleanupOnly =
          provider.getCloudCode() == CloudType.onprem
              && NodeAgentClient.isCloudTypeSupported(provider.getCloudCode());
      if (cleanupOnly) {
        // Use node agent to only clean up the node.
        NodeAgent nodeAgent =
            nodeAgentClient.getAndUpgradeOrThrow(nodeDetails.cloudInfo.private_ip);
        log.info(
            "Running destroy server for node {} with IP {} and node agent {}",
            taskParams().nodeName,
            nodeDetails.cloudInfo.private_ip,
            nodeAgent);
        DestroyServerInput.Builder builder = DestroyServerInput.newBuilder();
        builder.setIsProvisioningCleanup(!provider.getDetails().skipProvisioning);
        builder.setYbHomeDir(provider.getYbHome());
        nodeAgentClient.runDestroyServer(
            nodeAgent, builder.build(), NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      } else {
        // Terminate the instance.
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Destroy, taskParams())
            .processErrors();
      }
      cleanupFailed = false;
    } catch (Exception e) {
      if (!taskParams().isForceDelete) {
        throw e;
      } else {
        log.debug(
            "Ignoring error deleting instance {} due to isForceDelete being set.",
            taskParams().nodeName,
            e);
      }
    }

    if (taskParams().deleteRootVolumes && cloudType != Common.CloudType.onprem) {
      try {
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Delete_Root_Volumes, taskParams())
            .processErrors();
      } catch (Exception e) {
        if (!taskParams().isForceDelete) {
          throw e;
        } else {
          log.debug(
              "Ignoring error deleting volumes for {} due to isForceDelete being set.",
              taskParams().nodeName,
              e);
        }
      }
    }

    if (cloudType == Common.CloudType.onprem
        && nodeDetails.state != NodeDetails.NodeState.Decommissioned) {
      Optional<NodeInstance> nodeInstanceOpt =
          NodeInstance.maybeGetByName(taskParams().nodeName, taskParams().nodeUuid);
      if (nodeInstanceOpt.isPresent()) {
        if (cleanupFailed) {
          log.info(
              "Failed to clean node instance {}. Setting to decommissioned state",
              taskParams().nodeName);
          nodeInstanceOpt.get().setToFailedCleanup(universe, nodeDetails);
        } else {
          nodeInstanceOpt.get().clearNodeDetails();
          log.info("Marked node instance {} as available", taskParams().nodeName);
        }
      }
    }

    if (!cleanupFailed) {
      try {
        maybeUninstallNodeAgent(universe, nodeDetails);
        deleteNodeAgent(nodeDetails);
      } catch (Exception e) {
        if (!taskParams().isForceDelete) {
          throw e;
        } else {
          log.debug(
              "Ignoring error deleting node agent {} due to isForceDelete being set.",
              taskParams().nodeName,
              e);
        }
      }
    }
    if (!taskParams().skipUpdateNodeState) {
      // Update the node state to Terminated to mark that instance has been terminated. This is a
      // short-lived state as either the node is deleted or the state is changed to Decommissioned.
      setNodeState(NodeDetails.NodeState.Terminated);
    }
    if (taskParams().deleteNode) {
      removeNodeFromUniverse(taskParams().nodeName);
    }
  }
}
