// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import java.util.Arrays;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ManageOtelCollector extends NodeTaskBase {

  public static String OtelCollectorVersion = "0.90.0";
  public static String OtelCollectorPlatform = "linux";

  private final NodeUniverseManager nodeUniverseManager;
  private final NodeAgentRpcPayload nodeAgentRpcPayload;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected ManageOtelCollector(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      NodeAgentRpcPayload nodeAgentRpcPayload) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
  }

  public static class Params extends NodeTaskParams {
    public boolean installOtelCollector;
    public AuditLogConfig auditLogConfig;
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
    taskParams().useSudo = isTServerServiceSystemLevel(universe, node);

    log.info(
        "Managing OpenTelemetry collector on instance {} with useSudo set to {}",
        taskParams().nodeName,
        taskParams().useSudo);
    Optional<NodeAgent> optional =
        confGetter.getGlobalConf(GlobalConfKeys.nodeAgentDisableConfigureServer)
            ? Optional.empty()
            : nodeUniverseManager.maybeGetNodeAgent(
                getUniverse(), node, true /*check feature flag*/);

    if (optional.isPresent()) {
      log.info("Configuring otel-collector using node-agent");
      if (taskParams().otelCollectorEnabled && taskParams().auditLogConfig != null) {
        AuditLogConfig config = taskParams().auditLogConfig;
        if (!((config.getYsqlAuditConfig() == null || !config.getYsqlAuditConfig().isEnabled())
            && (config.getYcqlAuditConfig() == null || !config.getYcqlAuditConfig().isEnabled()))) {
          nodeAgentClient.runInstallOtelCollector(
              optional.get(),
              nodeAgentRpcPayload.setupInstallOtelCollectorBits(
                  universe, node, taskParams(), optional.get()),
              NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
        }
      }
    } else {
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Manage_Otel_Collector, taskParams())
          .processErrors();
    }
  }

  private boolean isTServerServiceSystemLevel(Universe universe, NodeDetails node) {
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
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
