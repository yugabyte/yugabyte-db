// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ManageOtelCollector extends NodeTaskBase {

  @Inject
  protected ManageOtelCollector(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public boolean installOtelCollector;
    public AuditLogConfig auditLogConfig;
    public Map<String, String> gflags;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Managing OpenTelemetry collector on instance {}", taskParams().nodeName);
    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Manage_Otel_Collector, taskParams())
        .processErrors();
  }
}
