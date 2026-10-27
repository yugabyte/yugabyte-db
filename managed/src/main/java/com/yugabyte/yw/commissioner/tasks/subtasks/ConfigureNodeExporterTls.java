// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.audit.otel.OtelCollectorUtil;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.nodeagent.ConfigureNodeExporterOutput;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Decides node_exporter's scheme for a whole universe (see {@link
 * Universe#shouldEnableNodeExporterHttps}) and, if it differs from the current one, switches every
 * node through node-agent's ConfigureNodeExporter RPC. Only tasks that already touch every node run
 * it. The switch is all-or-nothing: if a node fails, the nodes already switched are put back before
 * the task fails, and the flag, Prometheus targets and OTel configs change only after every node
 * did. A node configured later gets the universe's current scheme from AnsibleConfigureServers.
 *
 * <p>Must run after the node certificates are in place.
 */
@Slf4j
public class ConfigureNodeExporterTls extends UniverseTaskBase {

  // Universe config entry, "true" while Prometheus targets or OTel collectors may still use the
  // scheme the nodes served before the last switch.
  private static final String SCRAPERS_PENDING = "nodeExporterScrapersPending";

  private final SwamperHelper swamperHelper;
  private final NodeAgentRpcPayload nodeAgentRpcPayload;

  @Inject
  protected ConfigureNodeExporterTls(
      BaseTaskDependencies baseTaskDependencies,
      SwamperHelper swamperHelper,
      NodeAgentRpcPayload nodeAgentRpcPayload) {
    super(baseTaskDependencies);
    this.swamperHelper = swamperHelper;
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
  }

  @Override
  public void run() {
    refreshUnrecordedNodes();
    Universe universe = getUniverse();
    boolean https =
        Universe.shouldEnableNodeExporterHttps(
            universe,
            isNodeExporterTlsAllowed(confGetter, Customer.get(universe.getCustomerId()), universe));
    if (https != universe.isNodeExporterHttps()) {
      switchNodes(universe, https);
      // The scrapers read the flag from the universe, so it is saved first. The pending mark goes
      // with it and is cleared only once they follow, so a retry after a failed refresh redoes it.
      saveUniverseDetails(
          u ->
              u.updateConfig(
                  ImmutableMap.of(
                      Universe.NODE_EXPORTER_HTTPS,
                      String.valueOf(https),
                      SCRAPERS_PENDING,
                      "true")));
    } else if (!"true".equals(universe.getConfig().get(SCRAPERS_PENDING))) {
      return;
    }
    universe = getUniverse();
    if (config.getBoolean(MetricQueryHelper.PROMETHEUS_MANAGEMENT_ENABLED)) {
      swamperHelper.writeUniverseTargetJson(universe);
    }
    refreshOtelCollectors(universe);
    saveUniverseDetails(u -> u.updateConfig(ImmutableMap.of(SCRAPERS_PENDING, "false")));
  }

  /** Whether YBA's config lets the universe serve node_exporter over HTTPS. */
  public static boolean isNodeExporterTlsAllowed(
      RuntimeConfGetter confGetter, Customer customer, Universe universe) {
    // YBM runs its own collectors, which scrape the exporters over plain HTTP.
    return confGetter.getConfForScope(universe, UniverseConfKeys.nodeExporterTlsEnabled)
        && !confGetter.getConfForScope(customer, CustomerConfKeys.cloudEnabled);
  }

  private void switchNodes(Universe universe, boolean https) {
    log.info("Switching node_exporter on universe {} to {}", universe.getUniverseUUID(), https);
    List<NodeDetails> changed = new ArrayList<>();
    try {
      for (NodeDetails node : universe.getUniverseDetails().nodeDetailsSet) {
        if (Universe.isNodeExporterManaged(node)) {
          configureNode(universe, node, https);
          changed.add(node);
        }
      }
    } catch (RuntimeException e) {
      log.error("Switching node_exporter failed; putting {} nodes back", changed.size(), e);
      for (NodeDetails node : changed) {
        try {
          configureNode(universe, node, !https);
        } catch (RuntimeException re) {
          log.error("Could not put node_exporter on {} back", node.nodeName, re);
        }
      }
      throw e;
    }
  }

  // Re-reads every node not recorded as USER: YBA does not lay out manually provisioned nodes, so
  // an
  // operator may have converted one since, and an edit sent by a client that drops this INTERNAL
  // field resets it. USER nodes are not re-read: nothing YBA ships moves a node back to ROOT, and
  // catching drift by hand is the health check's job.
  private void refreshUnrecordedNodes() {
    Universe universe = getUniverse();
    for (NodeDetails node : universe.getUniverseDetails().nodeDetailsSet) {
      if (!Universe.isNodeExporterManaged(node)
          || node.nodeExporterSystemdLevel == NodeDetails.NodeExporterSystemdLevel.USER) {
        continue;
      }
      NodeAgent.maybeGetByIp(node.cloudInfo.private_ip)
          .flatMap(nodeAgent -> nodeAgentClient.detectNodeExporterSystemdLevel(nodeAgent))
          .filter(level -> level != node.nodeExporterSystemdLevel)
          .ifPresent(
              level ->
                  saveUniverseDetails(
                      u -> {
                        NodeDetails n = u.getNode(node.nodeName);
                        if (n != null) {
                          n.nodeExporterSystemdLevel = level;
                        }
                      }));
    }
  }

  // node-agent writes web.yml and restarts the exporter only when the scheme it serves changes;
  // the exporter re-reads the certificate on every handshake.
  private void configureNode(Universe universe, NodeDetails node, boolean https) {
    NodeAgent nodeAgent = nodeAgentClient.getAndUpgradeOrThrow(node.cloudInfo.private_ip);
    ConfigureNodeExporterOutput output =
        nodeAgentClient.runConfigureNodeExporter(
            nodeAgent,
            nodeAgentRpcPayload.setUpConfigureNodeExporterBits(universe, node, https),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
    log.info(
        "node_exporter on {} serves {} (restarted: {})",
        node.nodeName,
        output.getScheme(),
        output.getRestarted());
  }

  // The collector's node_exporter scrape config carries the scheme, so it follows a flip.
  private void refreshOtelCollectors(Universe universe) {
    if (!universe.getUniverseDetails().otelCollectorEnabled) {
      return;
    }
    List<Cluster> clusters = universe.getUniverseDetails().clusters;
    for (Cluster cluster : clusters) {
      if (cluster.userIntent.metricsExportConfig == null) {
        continue;
      }
      List<NodeDetails> nodes =
          universe.getNodesInCluster(cluster.uuid).stream()
              .filter(Universe::isNodeExporterManaged)
              .collect(Collectors.toList());
      for (NodeDetails node : nodes) {
        if (!NodeAgentClient.isCloudTypeSupported(cluster.getProviderCloudType(node))) {
          continue;
        }
        ManageOtelCollector.Params params = new ManageOtelCollector.Params();
        params.nodeName = node.nodeName;
        params.setUniverseUUID(universe.getUniverseUUID());
        params.azUuid = node.azUuid;
        params.otelCollectorEnabled = true;
        params.telemetryConfig = OtelCollectorUtil.getCurrentTelemetryConfig(universe);
        params.deviceInfo = cluster.userIntent.evaluateDeviceInfoForNode(node);
        params.gflags = GFlagsUtil.getGFlagsForNode(node, ServerType.TSERVER, cluster, clusters);
        NodeAgent nodeAgent = nodeAgentClient.getAndUpgradeOrThrow(node.cloudInfo.private_ip);
        nodeAgentClient.runInstallOtelCollector(
            nodeAgent,
            nodeAgentRpcPayload.setupInstallOtelCollectorBits(universe, node, params, nodeAgent),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      }
    }
  }
}
