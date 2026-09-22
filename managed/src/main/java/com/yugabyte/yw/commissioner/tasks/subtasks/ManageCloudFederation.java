// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeAgentClient;
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

  @Inject
  protected ManageCloudFederation(
      BaseTaskDependencies baseTaskDependencies, NodeAgentRpcPayload nodeAgentRpcPayload) {
    super(baseTaskDependencies);
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
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

    if (!NodeAgentClient.isCloudTypeSupported(cluster.userIntent.providerType)) {
      // Federation is driven through node-agent; without it there is nothing to do here.
      log.warn(
          "Skipping cloud federation on {}: node-agent not supported for provider type {}",
          taskParams().nodeName,
          cluster.userIntent.providerType);
      return;
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
}
