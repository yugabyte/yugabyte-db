// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistQueryLoggingConfig;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.QueryLogConfigParams;
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
@ITask.Retryable
public class ModifyQueryLoggingConfig extends UpgradeTaskBase {

  @Inject
  protected ModifyQueryLoggingConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected QueryLogConfigParams taskParams() {
    return (QueryLogConfigParams) taskParams;
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
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          List<UniverseDefinitionTaskParams.Cluster> curClusters =
              universe.getUniverseDetails().clusters;
          MastersAndTservers mastersAndTservers = getNodesToBeRestarted();
          for (UniverseDefinitionTaskParams.Cluster curCluster : curClusters) {
            // We only update tservers
            // Upgrade GFlags in all nodes. Also install
            // OpenTelemetry collector and configure as needed
            createConfigureQueryLoggingAndOtelCollectorTasks(
                universe,
                curCluster.userIntent,
                mastersAndTservers.getForCluster(curCluster.uuid).tserversList);
          }
          updateAndPersistQueryLoggingConfigTask();

          // Update the swamper target file.
          createSwamperTargetUpdateTask(false /* removeFile */);
        });
  }

  private void createConfigureQueryLoggingAndOtelCollectorTasks(
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> tServerNodes) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            (nodes, processTypes) -> {
              createNodesSubtasks(universe, userIntent, nodes, processTypes);
            },
            Collections.emptyList(),
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().isYbcInstalled());
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            (nodes, processTypes) -> {
              createNodesSubtasks(universe, userIntent, nodes, processTypes);
            },
            Collections.emptyList(),
            tServerNodes,
            RUN_BEFORE_STOPPING,
            taskParams().isYbcInstalled());
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(
            (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
              createNodesSubtasks(universe, userIntent, nodeList, processTypes);
              // TBD: Most probably we can't set query logging flags in memory. Need to check.
            },
            Collections.emptyList(),
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
    // Update query logging gflags in TServer configuration file.
    createUpdateConfigurationFileTask(userIntent, nodes, processTypes);

    // Install, configure and start/stop/restart otel collector, if needed.
    createManageOtelCollectorTasks(
        userIntent,
        nodes,
        taskParams().installOtelCollector,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.auditLogConfig,
        taskParams().queryLogConfig,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.metricsExportConfig,
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
    params.queryLogConfig = taskParams().queryLogConfig;
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  public void updateAndPersistQueryLoggingConfigTask() {
    TaskExecutor.SubTaskGroup subTaskGroup =
        createSubTaskGroup("UpdateAndPersistQueryLoggingConfig");
    UpdateAndPersistQueryLoggingConfig task = createTask(UpdateAndPersistQueryLoggingConfig.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }
}
