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
import com.yugabyte.yw.models.helpers.NodeDetails;
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

  public static class Params extends NodeTaskParams {
    // GCP Workload Identity Federation audience used to render the external_account credential on
    // this node (GCS-on-AWS). Sourced from the provider's federation config.
    public String gcsAudience;
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

    if (taskParams().enabled && providerCloud == CloudType.onprem) {
      detectAndPersistPhysicalCloud(universe, node);
    }

    NodeAgent nodeAgent = nodeAgentClient.getAndUpgradeOrThrow(node.cloudInfo.private_ip);
    log.info(
        "Configuring cloud federation (enabled={}) on {} using node-agent",
        taskParams().enabled,
        taskParams().nodeName);
    nodeAgentClient.runConfigureCloudFederation(
        nodeAgent,
        nodeAgentRpcPayload.setupConfigureCloudFederationBits(
            universe, node, taskParams(), nodeAgent),
        NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
  }

  /**
   * Records which cloud this on-prem node physically runs on. An on-prem provider reports every
   * node as "onprem", so the node's own metadata service is the only thing that says whether it is
   * an AWS or a GCP VM - which is what a mixed on-prem universe needs to pick each node's
   * federation direction.
   *
   * <p>Best-effort: a node that cannot be probed is recorded as unknown and federation setup
   * carries on. The value is always written, so a node whose underlying machine was replaced cannot
   * keep a stale one.
   */
  private void detectAndPersistPhysicalCloud(Universe universe, NodeDetails node) {
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
  }
}
