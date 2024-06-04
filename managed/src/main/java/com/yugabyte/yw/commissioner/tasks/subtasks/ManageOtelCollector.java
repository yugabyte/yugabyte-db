// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import java.util.Arrays;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ManageOtelCollector extends NodeTaskBase {

  private final NodeUniverseManager nodeUniverseManager;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected ManageOtelCollector(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
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
    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Manage_Otel_Collector, taskParams())
        .processErrors();
  }
}
