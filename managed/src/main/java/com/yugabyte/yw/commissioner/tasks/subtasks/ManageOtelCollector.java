// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import java.util.Arrays;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ManageOtelCollector extends NodeTaskBase {

  public static String OtelCollectorVersion = "0.90.0";
  public static String OtelCollectorPlatform = "linux";

  private final NodeAgentRpcPayload nodeAgentRpcPayload;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected ManageOtelCollector(
      BaseTaskDependencies baseTaskDependencies, NodeAgentRpcPayload nodeAgentRpcPayload) {
    super(baseTaskDependencies);
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
  }

  public static class Params extends NodeTaskParams {
    public boolean installOtelCollector;
    public AuditLogConfig auditLogConfig;
    public QueryLogConfig queryLogConfig;
    public MetricsExportConfig metricsExportConfig;
    public Map<String, String> gflags;
    public boolean useSudo = false;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    Cluster nodeCluster = universe.getCluster(node.placementUuid);
    taskParams().useSudo =
        isTServerServiceSystemLevel(universe, node) && taskParams().installOtelCollector;

    log.info(
        "Managing OpenTelemetry collector on instance {} with useSudo set to {}",
        taskParams().nodeName,
        taskParams().useSudo);
    boolean isNodeAgentSupported =
        NodeAgentClient.isCloudTypeSupported(nodeCluster.userIntent.providerType);
    if (isNodeAgentSupported) {
      NodeAgent nodeAgent = nodeAgentClient.getAndUpgradeOrThrow(node.cloudInfo.private_ip);
      log.info("Configuring otel-collector using node-agent");
      if (taskParams().otelCollectorEnabled) {
        nodeAgentClient.runInstallOtelCollector(
            nodeAgent,
            nodeAgentRpcPayload.setupInstallOtelCollectorBits(
                universe, node, taskParams(), nodeAgent),
            NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
      }
    } else {
      log.info("Configuring otel-collector using legacy mode without node-agent");
      getNodeManager()
          .nodeCommand(NodeCommandType.Manage_Otel_Collector, taskParams())
          .processErrors();
    }
  }

  private boolean isTServerServiceSystemLevel(Universe universe, NodeDetails node) {
    Provider provider = Util.getProviderForNode(node, universe);
    String ybHomeDir = provider.getYbHome();
    log.debug("Using ybHomeDir {} to check for the tserver service unit file", ybHomeDir);
    String serviceFilePath = String.format("%s/.config/systemd/user/yb-tserver.service", ybHomeDir);
    // Build a command that always exits 0 upon successful execution and prints either
    // present or absent.
    String checkCmd =
        String.format("if [ -f \"%s\" ]; then echo present; else echo absent; fi", serviceFilePath);
    ShellResponse result =
        nodeUniverseManager.runCommand(
            node, universe, Arrays.asList("bash", "-c", checkCmd), shellContext);
    // result.isSuccess() means the command executed, not the file exists.
    if (!result.isSuccess()) {
      throw new RuntimeException(
          String.format(
              "Failed to check for service file %s. Error: %s",
              serviceFilePath, result.getMessage()));
    }
    String output = result.getMessage() == null ? "" : result.getMessage().trim();
    return "absent".equalsIgnoreCase(output);
  }
}
