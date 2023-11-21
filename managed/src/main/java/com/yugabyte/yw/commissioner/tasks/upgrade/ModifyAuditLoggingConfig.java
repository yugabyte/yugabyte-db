// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageOtelCollector;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistAuditLoggingConfig;
import com.yugabyte.yw.forms.AuditLogConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.*;
import javax.inject.Inject;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@EqualsAndHashCode(callSuper = false)
public class ModifyAuditLoggingConfig extends UpgradeTaskBase {

  @Inject
  protected ModifyAuditLoggingConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected AuditLogConfigParams taskParams() {
    return (AuditLogConfigParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.Provisioning;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.Reprovisioning;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          List<UniverseDefinitionTaskParams.Cluster> curClusters =
              universe.getUniverseDetails().clusters;
          for (UniverseDefinitionTaskParams.Cluster curCluster : curClusters) {
            // We only update tservers
            List<NodeDetails> tServerNodes =
                fetchTServerNodes(taskParams().upgradeOption).stream()
                    .filter(nodeDetails -> nodeDetails.isTserver)
                    .toList();
            // Upgrade GFlags in all nodes. Also install
            // OpenTelemetry collector and configure as needed
            createConfigureAuditLoggingAndOtelCollectorTasks(curCluster.userIntent, tServerNodes);
          }
          updateAndPersistAuditLoggingConfigTask();
        });
  }

  private void createConfigureAuditLoggingAndOtelCollectorTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent, List<NodeDetails> tServerNodes) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            (nodes, processTypes) -> {
              createNodesSubtasks(userIntent, nodes, processTypes);
            },
            Collections.emptyList(),
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().isYbcInstalled());
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            (nodes, processTypes) -> {
              createNodesSubtasks(userIntent, nodes, processTypes);
            },
            Collections.emptyList(),
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().isYbcInstalled());
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(
            (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
              createNodesSubtasks(userIntent, nodeList, processTypes);
              // TBD: Most probably we can't set audit logging flags in memory. Need to check.
            },
            Collections.emptyList(),
            tServerNodes,
            DEFAULT_CONTEXT);
        break;
    }
  }

  protected void createNodesSubtasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes) {
    // Install, configure and start/stop/restart otel collector, if needed.
    createManageOtelCollectorTasks(userIntent, nodes);

    // Update audit logging gflags in TServer configuration file.
    createUpdateConfigurationFileTask(userIntent, nodes, processTypes);
  }

  protected void createUpdateConfigurationFileTask(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.UpdatingGFlags, taskParams().nodePrefix);
    TaskExecutor.SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      ServerType processType = getSingle(processTypes);
      subTaskGroup.addSubTask(getAnsibleConfigureServerTask(userIntent, node, processType));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected void createManageOtelCollectorTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent, List<NodeDetails> nodes) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.ManageOtelCollector, taskParams().nodePrefix);
    TaskExecutor.SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      ManageOtelCollector.Params params = new ManageOtelCollector.Params();
      params.nodeName = node.nodeName;
      params.setUniverseUUID(taskParams().getUniverseUUID());
      params.azUuid = node.azUuid;
      params.installOtelCollector = taskParams().installOtelCollector;
      params.otelCollectorEnabled =
          taskParams().installOtelCollector
              || getUniverse().getUniverseDetails().otelCollectorEnabled;
      params.auditLogConfig = taskParams().auditLogConfig;
      params.deviceInfo = userIntent.getDeviceInfoForNode(node);
      ManageOtelCollector task = createTask(ManageOtelCollector.class);
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ManageOtelCollector);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected AnsibleConfigureServers getAnsibleConfigureServerTask(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      NodeDetails node,
      ServerType processType) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            userIntent,
            node,
            processType,
            UpgradeTaskParams.UpgradeTaskType.GFlags,
            UpgradeTaskParams.UpgradeTaskSubType.None);
    params.auditLogConfig = taskParams().auditLogConfig;
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  public void updateAndPersistAuditLoggingConfigTask() {
    TaskExecutor.SubTaskGroup subTaskGroup =
        createSubTaskGroup("UpdateAndPersistAuditLoggingConfig");
    UpdateAndPersistAuditLoggingConfig task = createTask(UpdateAndPersistAuditLoggingConfig.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }
}
