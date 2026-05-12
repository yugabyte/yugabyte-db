// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistExportTelemetryConfig;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import java.util.Set;
import javax.inject.Inject;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@EqualsAndHashCode(callSuper = false)
@ITask.Retryable
public class ConfigureExportTelemetryConfig extends UpgradeTaskBase {

  @Inject
  protected ConfigureExportTelemetryConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected ExportTelemetryConfigParams taskParams() {
    return (ExportTelemetryConfigParams) taskParams;
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
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodes(taskParams().upgradeOption);
  }

  @Override
  public void run() {
    log.info(
        "Running ConfigureExportTelemetryConfig for universe: {}", getUniverse().getUniverseUUID());
    log.info("taskParams: {}", taskParams().toString());
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          List<UniverseDefinitionTaskParams.Cluster> curClusters =
              universe.getUniverseDetails().clusters;
          MastersAndTservers mastersAndTservers = getNodesToBeRestarted();

          // Run OTEL on all nodes (masters and tservers) per cluster.
          for (UniverseDefinitionTaskParams.Cluster curCluster : curClusters) {
            createConfigureTelemetryAndOtelCollectorTasks(
                universe,
                curCluster.userIntent,
                mastersAndTservers.getForCluster(curCluster.uuid).mastersList,
                mastersAndTservers.getForCluster(curCluster.uuid).tserversList);
          }

          // Update the export telemetry config in the PG DB table and sync to universe details.
          createUpdateAndPersistExportTelemetryConfigTask();

          // Update the swamper target file.
          createSwamperTargetUpdateTask(false /* removeFile */);
        });
  }

  private void createConfigureTelemetryAndOtelCollectorTasks(
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            (nodes, processTypes) -> createNodesSubtasks(universe, userIntent, nodes, processTypes),
            masterNodes,
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().isYbcInstalled());
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            (nodes, processTypes) -> createNodesSubtasks(universe, userIntent, nodes, processTypes),
            masterNodes,
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().isYbcInstalled());
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(
            (List<NodeDetails> nodeList, Set<ServerType> processTypes) ->
                createNodesSubtasks(universe, userIntent, nodeList, processTypes),
            masterNodes,
            tServerNodes,
            DEFAULT_CONTEXT);
        break;
    }
  }

  protected void createNodesSubtasks(
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes) {
    // Update telemetry logging gflags in TServer configuration file.
    createUpdateConfigurationFileTask(userIntent, nodes, processTypes);

    // Install, configure and start/stop/restart otel collector when needed.
    createManageOtelCollectorTasks(
        userIntent,
        nodes,
        true, // unified export-telemetry-config flow always installs OTEL collector when needed
        taskParams().getAuditLogConfig(),
        taskParams().getQueryLogConfig(),
        taskParams().getMetricsExportConfig(),
        nodeDetails ->
            GFlagsUtil.getGFlagsForNode(
                nodeDetails,
                ServerType.TSERVER,
                universe.getCluster(nodeDetails.placementUuid),
                universe.getUniverseDetails().clusters));
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
    params.auditLogConfig = taskParams().getAuditLogConfig();
    params.queryLogConfig = taskParams().getQueryLogConfig();
    params.metricsExportConfig = taskParams().getMetricsExportConfig();
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  public void createUpdateAndPersistExportTelemetryConfigTask() {
    TaskExecutor.SubTaskGroup subTaskGroup =
        createSubTaskGroup("UpdateAndPersistExportTelemetryConfig");
    UpdateAndPersistExportTelemetryConfig task =
        createTask(UpdateAndPersistExportTelemetryConfig.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }
}
