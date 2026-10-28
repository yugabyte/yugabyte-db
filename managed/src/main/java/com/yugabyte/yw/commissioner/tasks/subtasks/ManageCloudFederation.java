// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeCloudDetector;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.List;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Configures (or tears down) cross-cloud federated IAM on a DB node via node-agent. Mirrors {@link
 * ManageOtelCollector}: the node-agent RPC does the on-node work.
 */
@Slf4j
public class ManageCloudFederation extends NodeTaskBase {

  private final NodeAgentRpcPayload nodeAgentRpcPayload;
  private final NodeCloudDetector nodeCloudDetector;

  @Inject
  protected ManageCloudFederation(
      BaseTaskDependencies baseTaskDependencies,
      NodeAgentRpcPayload nodeAgentRpcPayload,
      NodeCloudDetector nodeCloudDetector) {
    super(baseTaskDependencies);
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
    this.nodeCloudDetector = nodeCloudDetector;
  }

  /**
   * Carries every storage cloud the provider is configured for, not one resolved target: which one
   * this node needs depends on the cloud it runs on, which for an on-prem provider is only known
   * once {@link #run()} has probed the node.
   */
  public static class Params extends NodeTaskParams {
    public List<CrossCloudFederationTarget> targets;
    // When false, federation artifacts on this node are torn down.
    public boolean enabled = true;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    Cluster cluster = universe.getCluster(node.placementUuid);
    // Resolved per node: on a cluster that spans providers, userIntent.providerType is backfilled
    // from one arbitrary provider and so does not describe this node.
    CloudType providerCloud = cluster.getProviderCloudType(node);

    if (!NodeAgentClient.isCloudTypeSupported(providerCloud)) {
      // Federation is driven through node-agent; without it there is nothing to do here.
      log.warn(
          "Skipping cloud federation on {}: node-agent not supported for provider type {}",
          taskParams().nodeName,
          providerCloud);
      return;
    }

    NodeAgent nodeAgent = nodeAgentClient.getAndUpgradeOrThrow(node.cloudInfo.private_ip);

    if (!taskParams().enabled) {
      // Teardown drops the whole federation directory, so it needs no source or target: a node
      // configured for any cloud is cleaned up the same way.
      log.info("Tearing down cloud federation on {} using node-agent", taskParams().nodeName);
      nodeAgentClient.runConfigureCloudFederation(
          nodeAgent,
          nodeAgentRpcPayload.setupConfigureCloudFederationBits(
              universe, node, taskParams(), null, null, nodeAgent),
          NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      return;
    }

    // For aws/gcp the provider already names the cloud every node runs on. For on-prem it does
    // not, so the node's own metadata service is what decides.
    CloudType sourceCloud =
        providerCloud == CloudType.onprem
            ? detectAndPersistPhysicalCloud(universe, node)
            : providerCloud;
    CrossCloudFederationTarget target = resolveTarget(sourceCloud);
    if (target == null) {
      // Nothing for this node to do: every cloud its provider configures is one it reaches
      // natively, or it is not a cloud VM at all. Skipping keeps a provider that carries a single
      // target usable by a universe whose nodes span both clouds.
      log.info(
          "Skipping cloud federation on {}: it runs on {} and its provider configures only {}",
          taskParams().nodeName,
          sourceCloud,
          configuredTargetClouds());
      return;
    }

    log.info(
        "Configuring cloud federation on {} ({} node reaching {}) using node-agent",
        taskParams().nodeName,
        sourceCloud,
        target.targetCloud);
    nodeAgentClient.runConfigureCloudFederation(
        nodeAgent,
        nodeAgentRpcPayload.setupConfigureCloudFederationBits(
            universe, node, taskParams(), sourceCloud, target, nodeAgent),
        NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
  }

  /**
   * The one target this node needs, or null when it needs none.
   *
   * <p>Fails only when the node's cloud could not be determined - configuring a guess would write
   * credentials that cannot work, and would hide a node that should have been federated. A node
   * whose cloud IS known but has no foreign target simply has nothing to do: it reaches every cloud
   * its provider configures natively, or it is not a cloud VM at all. That is a legitimate
   * configuration, not an error, and skipping it is what lets one provider carry a single target
   * while its universe spans both clouds.
   */
  @Nullable
  private CrossCloudFederationTarget resolveTarget(@Nullable CloudType sourceCloud) {
    if (sourceCloud == null) {
      throw new IllegalStateException(
          String.format(
              "Cannot configure federated IAM on %s: the cloud it runs on could not be determined,"
                  + " so any direction chosen for it would be a guess. Check that the node can"
                  + " reach its instance metadata service.",
              taskParams().nodeName));
    }
    // Real hardware cannot present a cloud identity, so there is no federation to set up.
    if (sourceCloud != CloudType.aws && sourceCloud != CloudType.gcp) {
      return null;
    }
    // A node needs federation only for the clouds it is not running on; same-cloud access is
    // native and must not be replaced with federated credentials.
    List<CrossCloudFederationTarget> foreign =
        taskParams().targets == null
            ? Collections.emptyList()
            : taskParams().targets.stream()
                .filter(t -> t != null && t.isUsable() && t.targetCloud != sourceCloud)
                .collect(Collectors.toList());
    if (foreign.isEmpty()) {
      return null;
    }
    if (foreign.size() > 1) {
      // The node holds one set of federation artifacts, so it can serve one foreign cloud. Reached
      // only once a third cloud is supported; fail loudly rather than silently pick one.
      throw new IllegalStateException(
          String.format(
              "Node %s runs on %s and its provider configures federated IAM for %s. Configuring a"
                  + " node for more than one storage cloud is not supported yet.",
              taskParams().nodeName, sourceCloud, configuredTargetClouds()));
    }
    return foreign.get(0);
  }

  private String configuredTargetClouds() {
    if (taskParams().targets == null || taskParams().targets.isEmpty()) {
      return "no cloud";
    }
    return taskParams().targets.stream()
        .map(t -> String.valueOf(t.targetCloud))
        .collect(Collectors.joining(", "));
  }

  /**
   * Records which cloud this on-prem node physically runs on, and returns it. An on-prem provider
   * reports every node as "onprem", so the node's own metadata service is the only thing that says
   * whether it is an AWS or a GCP VM - which is what a mixed on-prem universe needs to pick each
   * node's federation target.
   *
   * <p>The value is always written, so a node whose underlying machine was replaced cannot keep a
   * stale one. A node that could not be probed is recorded as unknown, and {@link #resolveTarget}
   * then fails the subtask rather than configuring a guess.
   */
  @Nullable
  private CloudType detectAndPersistPhysicalCloud(Universe universe, NodeDetails node) {
    CloudType detected =
        nodeCloudDetector.detect(
            universe,
            node,
            confGetter.getConfForScope(universe, UniverseConfKeys.nodeCloudDetectionTimeout));
    log.info("Physical cloud of node {} detected as {}", taskParams().nodeName, detected);
    String physicalCloud = detected == null ? null : detected.name();
    saveUniverseDetails(
        u -> {
          NodeDetails n = u.getNode(taskParams().nodeName);
          if (n != null && n.cloudInfo != null) {
            n.cloudInfo.physicalCloud = physicalCloud;
          }
        });
    return detected;
  }
}
