// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeCloudDetector;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CrossCloudFederationDirections;
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

    // For aws/gcp/oci the provider already names the cloud every node runs on. For on-prem it does
    // not, so the node's own metadata service decides; it reports only aws, gcp or onprem, so OCI
    // here always means an OCI provider, whose identity domain URL applyFederationDirection
    // requires.
    CloudType sourceCloud =
        providerCloud == CloudType.onprem
            ? detectAndPersistPhysicalCloud(universe, node)
            : providerCloud;
    String ociSkipReason =
        ociSourceSkipReason(
            sourceCloud, confGetter.getGlobalConf(GlobalConfKeys.enableOciFederationSource));
    if (ociSkipReason != null) {
      log.warn(
          "Skipping cloud federation on {}: it runs on OCI but {}",
          taskParams().nodeName,
          ociSkipReason);
      return;
    }
    if (sourceCloud == CloudType.oci
        && !Util.areYbVersionsEqual(Util.getYbaVersion(), nodeAgent.getVersion(), true)) {
      // An older node-agent ignores the OCI source and reports success on credentials that fail.
      throw new IllegalStateException(
          String.format(
              "Node-agent on %s is version %s but YBA is %s. Upgrade node-agent before federating a"
                  + " node on OCI.",
              taskParams().nodeName, nodeAgent.getVersion(), Util.getYbaVersion()));
    }
    CrossCloudFederationTarget target =
        selectTarget(
            taskParams().nodeName,
            sourceCloud,
            providerCloud != CloudType.onprem,
            taskParams().targets);
    if (target == null) {
      // Nothing for this node to do: every cloud its provider configures is one it reaches
      // natively or cannot federate to (selectTarget warns about those), or the node is
      // not a cloud VM at all. Skipping keeps a provider that carries a single target usable by a
      // universe whose nodes span clouds.
      log.info(
          "Skipping cloud federation on {}: it runs on {} and none of what its provider"
              + " configures ({}) needs federation from there",
          taskParams().nodeName,
          sourceCloud,
          configuredTargetClouds(taskParams().targets));
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
   * Why a node on OCI is left without federation, or null when it can be configured. It is left
   * alone rather than failed, so turning the gate off does not break operations on a universe that
   * already has OCI federation set up.
   */
  @Nullable
  static String ociSourceSkipReason(@Nullable CloudType sourceCloud, boolean ociSourceEnabled) {
    if (sourceCloud != CloudType.oci || ociSourceEnabled) {
      return null;
    }
    return String.format(
        "federated IAM from OCI nodes is not enabled (runtime config %s)",
        GlobalConfKeys.enableOciFederationSource.getKey());
  }

  /**
   * The one target a node on {@code sourceCloud} needs, or null when it needs none.
   *
   * <p>Fails when the node's cloud could not be determined - configuring a guess would write
   * credentials that cannot work - and when the provider names the node's cloud yet configures a
   * target that cloud cannot federate to. A node whose cloud IS known but has no foreign target has
   * nothing to do, which is what lets one provider carry a single target while its universe spans
   * several clouds.
   *
   * @param sourceFixedByProvider true when the provider names {@code sourceCloud} (AWS, GCP, OCI),
   *     false for on-prem, where the node's metadata service decides
   */
  @Nullable
  static CrossCloudFederationTarget selectTarget(
      String nodeName,
      @Nullable CloudType sourceCloud,
      boolean sourceFixedByProvider,
      @Nullable List<CrossCloudFederationTarget> targets) {
    if (sourceCloud == null) {
      throw new IllegalStateException(
          String.format(
              "Cannot configure federated IAM on %s: the cloud it runs on could not be determined,"
                  + " so any direction chosen for it would be a guess. Check that the node can"
                  + " reach its instance metadata service.",
              nodeName));
    }
    // Real hardware cannot present a cloud identity, so there is no federation to set up.
    if (!CrossCloudFederationDirections.isSource(sourceCloud)) {
      return null;
    }
    // A node needs federation only for the clouds it is not running on; same-cloud access is
    // native and must not be replaced with federated credentials.
    List<CrossCloudFederationTarget> foreign =
        targets == null
            ? Collections.emptyList()
            : targets.stream()
                .filter(t -> t != null && t.isUsable() && t.targetCloud != sourceCloud)
                .collect(Collectors.toList());
    // Of those, only the clouds this node can federate to.
    List<CrossCloudFederationTarget> reachable =
        foreign.stream()
            .filter(t -> CrossCloudFederationDirections.isSupported(sourceCloud, t.targetCloud))
            .collect(Collectors.toList());
    if (reachable.size() < foreign.size()) {
      List<String> unreachable =
          foreign.stream()
              .filter(t -> !reachable.contains(t))
              .map(t -> String.valueOf(t.targetCloud))
              .collect(Collectors.toList());
      // Every node of a provider that names the node's cloud is there, so such a target can never
      // be served. Validation refuses it at save but can be bypassed (validate=false,
      // ignoreValidationErrors=true); fail rather than leave the node without credentials for it.
      if (sourceFixedByProvider) {
        throw new IllegalStateException(
            String.format(
                "Node %s runs on %s and cannot federate to %s, yet its provider"
                    + " configures a federated target there. Remove that target from the provider.",
                nodeName, sourceCloud, unreachable));
      }
      // The node's cloud was detected and the provider serves nodes of other clouds too: leave it.
      log.warn(
          "Node {} runs on {} and cannot federate to {}; a backup from this node to a"
              + " bucket there will not work",
          nodeName,
          sourceCloud,
          unreachable);
    }
    if (reachable.isEmpty()) {
      return null;
    }
    if (reachable.size() > 1) {
      // The node holds one set of federation artifacts, so it can serve one foreign target. More
      // than one is reached when two entries name the same cloud (the validators forbid it, but a
      // provider saved before they did may still have it) or, once a source has several directions,
      // when the provider names several clouds. Fail loudly rather than silently pick one.
      throw new IllegalStateException(
          String.format(
              "Node %s runs on %s and its provider has more than one federated target it can use"
                  + " (%s). A node can be configured for only one.",
              nodeName, sourceCloud, configuredTargetClouds(reachable)));
    }
    return reachable.get(0);
  }

  private static String configuredTargetClouds(@Nullable List<CrossCloudFederationTarget> targets) {
    if (targets == null || targets.isEmpty()) {
      return "no cloud";
    }
    return targets.stream()
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
   * stale one. A node that could not be probed is recorded as unknown, and {@link #selectTarget}
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
