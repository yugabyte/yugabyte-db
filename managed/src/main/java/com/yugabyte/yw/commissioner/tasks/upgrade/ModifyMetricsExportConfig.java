// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistMetricsExportConfig;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.MetricsExportConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
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
public class ModifyMetricsExportConfig extends UpgradeTaskBase {

  @Inject
  protected ModifyMetricsExportConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected MetricsExportConfigParams taskParams() {
    return (MetricsExportConfigParams) taskParams;
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
    return fetchNodes(UpgradeOption.NON_RESTART_UPGRADE);
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
            // We need to update both masters and tservers for metrics export configuration
            // Configure metrics export settings in all nodes

            createConfigureMetricsExportTasks(
                universe, curCluster.userIntent, mastersAndTservers.getForCluster(curCluster.uuid));
          }
          updateAndPersistMetricsExportConfigTask();

          // Update the swamper target file.
          createSwamperTargetUpdateTask(false /* removeFile */);
        });
  }

  private void createConfigureMetricsExportTasks(
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      MastersAndTservers mastersAndTservers) {
    // Updating the metrics export configuration on the nodes doesn't require restart.
    createNonRestartUpgradeTaskFlow(
        (List<NodeDetails> nodeList, Set<ServerType> processTypes) -> {
          createNodesSubtasks(universe, userIntent, nodeList, processTypes);
        },
        new ArrayList<>(mastersAndTservers.getAllNodes()),
        Collections.emptyList(),
        DEFAULT_CONTEXT);
  }

  protected void createNodesSubtasks(
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes) {
    // Configure metrics collection and export settings
    createManageOtelCollectorTasks(
        userIntent,
        nodes,
        taskParams().installOtelCollector,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.auditLogConfig,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.queryLogConfig,
        taskParams().metricsExportConfig,
        nodeDetails ->
            GFlagsUtil.getGFlagsForNode(
                nodeDetails,
                ServerType.TSERVER,
                universe.getCluster(nodeDetails.placementUuid),
                universe.getUniverseDetails().clusters));
  }

  public void updateAndPersistMetricsExportConfigTask() {
    TaskExecutor.SubTaskGroup subTaskGroup =
        createSubTaskGroup("UpdateAndPersistMetricsExportConfig");
    UpdateAndPersistMetricsExportConfig task =
        createTask(UpdateAndPersistMetricsExportConfig.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }
}
