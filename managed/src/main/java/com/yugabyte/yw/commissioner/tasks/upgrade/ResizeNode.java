// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateNodeDetails;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.Objects;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public class ResizeNode extends UpgradeTaskBase {

  @Inject
  protected ResizeNode(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected ResizeNodeParams taskParams() {
    return (ResizeNodeParams) super.taskParams();
  }

  @Override
  public UserTaskDetails.SubTaskGroupType getTaskSubGroupType() {
    return UserTaskDetails.SubTaskGroupType.ResizingDisk;
  }

  @Override
  public NodeDetails.NodeState getNodeState() {
    return NodeDetails.NodeState.Resizing;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          // Verify the request params and fail if invalid.
          taskParams().verifyParams(universe);
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodesForCluster();
          // Create task sequence for VM Image upgrade.
          final UniverseDefinitionTaskParams.UserIntent userIntent =
              taskParams().getPrimaryCluster().userIntent;
          String newInstanceType = userIntent.instanceType;
          UniverseDefinitionTaskParams.UserIntent currentIntent =
              universe.getUniverseDetails().getPrimaryCluster().userIntent;

          final boolean instanceTypeIsChanging =
              !Objects.equals(
                      newInstanceType,
                      universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType)
                  || taskParams().isForceResizeNode();

          if (instanceTypeIsChanging) {
            Set<NodeDetails> nodez = new HashSet<>(nodes.getLeft());
            nodez.addAll(nodes.getRight());
            createPreResizeNodeTasks(nodez, currentIntent.instanceType, currentIntent.deviceInfo);
          }

          createRollingNodesUpgradeTaskFlow(
              (nodez, processTypes) ->
                  createResizeNodeTasks(nodez, universe, instanceTypeIsChanging),
              nodes,
              new UpgradeContext(userIntent.replicationFactor > 1, false));

          Integer newDiskSize = null;
          if (taskParams().getPrimaryCluster().userIntent.deviceInfo != null) {
            newDiskSize = taskParams().getPrimaryCluster().userIntent.deviceInfo.volumeSize;
          }
          // Persist changes in the universe.
          createPersistResizeNodeTask(
                  newInstanceType,
                  newDiskSize,
                  taskParams().clusters.stream().map(c -> c.uuid).collect(Collectors.toList()))
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
        });
  }

  private void createPreResizeNodeTasks(
      Collection<NodeDetails> nodes, String currentInstanceType, DeviceInfo currentDeviceInfo) {
    // Update mounted disks.
    for (NodeDetails node : nodes) {
      if (!node.disksAreMountedByUUID) {
        createUpdateMountedDisksTask(node, currentInstanceType, currentDeviceInfo)
            .setSubTaskGroupType(getTaskSubGroupType());
      }
    }
  }

  private void createResizeNodeTasks(
      List<NodeDetails> nodes, Universe universe, boolean instanceTypeIsChanging) {

    UniverseDefinitionTaskParams.UserIntent currUserIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;

    Integer currDiskSize = currUserIntent.deviceInfo.volumeSize;
    String currInstanceType = currUserIntent.instanceType;
    // Todo: Add preflight checks here

    // Change disk size.
    DeviceInfo deviceInfo = taskParams().getPrimaryCluster().userIntent.deviceInfo;
    if (deviceInfo != null) {
      Integer newDiskSize = deviceInfo.volumeSize;
      // Check if the storage needs to be resized.
      if (taskParams().isForceResizeNode() || !currDiskSize.equals(newDiskSize)) {
        log.info("Resizing disk from {} to {}", currDiskSize, newDiskSize);

        // Resize the nodes' disks.
        createUpdateDiskSizeTasks(nodes)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ResizingDisk);
      } else {
        log.info(
            "Skipping resizing disk as both old and new sizes are {}, "
                + "and forceResizeNode flag is false",
            currDiskSize);
      }
    }

    // Change instance type
    String newInstanceType = taskParams().getPrimaryCluster().userIntent.instanceType;
    if (instanceTypeIsChanging) {
      for (NodeDetails node : nodes) {
        // Check if the node needs to be resized.
        if (!taskParams().isForceResizeNode()
            && node.cloudInfo.instance_type.equals(newInstanceType)) {
          log.info("Skipping node {} as its type is already {}", node.nodeName, currInstanceType);
          continue;
        }

        // Change the instance type.
        createChangeInstanceTypeTask(node)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);

        // Persist the new instance type in the node details.
        node.cloudInfo.instance_type = newInstanceType;
        createNodeDetailsUpdateTask(node)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
      }
    }
  }

  private SubTaskGroup createChangeInstanceTypeTask(NodeDetails node) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("ChangeInstanceType", executor);
    ChangeInstanceType.Params params = new ChangeInstanceType.Params();

    params.nodeName = node.nodeName;
    params.universeUUID = taskParams().universeUUID;
    params.azUuid = node.azUuid;
    params.instanceType = taskParams().getPrimaryCluster().userIntent.instanceType;

    ChangeInstanceType changeInstanceTypeTask = createTask(ChangeInstanceType.class);
    changeInstanceTypeTask.initialize(params);
    subTaskGroup.addSubTask(changeInstanceTypeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createNodeDetailsUpdateTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("UpdateNodeDetails", executor);
    UpdateNodeDetails.Params updateNodeDetailsParams = new UpdateNodeDetails.Params();
    updateNodeDetailsParams.universeUUID = taskParams().universeUUID;
    updateNodeDetailsParams.azUuid = node.azUuid;
    updateNodeDetailsParams.nodeName = node.nodeName;
    updateNodeDetailsParams.details = node;

    UpdateNodeDetails updateNodeTask = createTask(UpdateNodeDetails.class);
    updateNodeTask.initialize(updateNodeDetailsParams);
    updateNodeTask.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(updateNodeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
