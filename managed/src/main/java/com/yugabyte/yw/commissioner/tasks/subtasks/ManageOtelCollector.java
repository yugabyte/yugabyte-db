// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import java.util.Arrays;
import java.util.Map;
import java.util.Optional;
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
    boolean output =
        nodeUniverseManager
            .runCommand(
                node,
                universe,
                Arrays.asList("systemctl", "status", "yb-tserver", "--user"),
                shellContext)
            .isSuccess();
    if (!output) {
      // Install otel collector as root level systemd.
      taskParams().useSudo = true;
    }
    log.info("Managing OpenTelemetry collector on instance {}", taskParams().nodeName);
    Optional<NodeAgent> optional =
        confGetter.getGlobalConf(GlobalConfKeys.nodeAgentEnableConfigureServer)
            ? nodeUniverseManager.maybeGetNodeAgent(
                getUniverse(), node, true /*check feature flag*/)
            : Optional.empty();

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
}
