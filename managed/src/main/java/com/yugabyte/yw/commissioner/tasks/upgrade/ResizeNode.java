// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

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
  public void validateParams() {
    taskParams().verifyParams(getUniverse());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          LinkedHashSet<NodeDetails> nodes = fetchNodesForCluster();
          // Create task sequence to resize nodes.
          for (UniverseDefinitionTaskParams.Cluster cluster : taskParams().clusters) {
            LinkedHashSet<NodeDetails> clusterNodes =
                nodes
                    .stream()
                    .filter(n -> cluster.uuid.equals(n.placementUuid))
                    .collect(Collectors.toCollection(LinkedHashSet::new));

            final UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;

            String newInstanceType = userIntent.instanceType;
            UniverseDefinitionTaskParams.UserIntent currentIntent =
                universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;

            final boolean instanceTypeIsChanging =
                !Objects.equals(newInstanceType, currentIntent.instanceType)
                    || taskParams().isForceResizeNode();

            if (instanceTypeIsChanging) {
              createPreResizeNodeTasks(nodes, currentIntent.instanceType, currentIntent.deviceInfo);
              createRollingNodesUpgradeTaskFlow(
                  (nodez, processTypes) ->
                      createResizeNodeTasks(nodez, universe, instanceTypeIsChanging, cluster),
                  clusterNodes,
                  UpgradeContext.builder()
                      .reconfigureMaster(userIntent.replicationFactor > 1)
                      .runBeforeStopping(false)
                      .processInactiveMaster(false)
                      .postAction(
                          node -> {
                            if (instanceTypeIsChanging) {
                              // Persist the new instance type in the node details.
                              node.cloudInfo.instance_type = newInstanceType;
                              createNodeDetailsUpdateTask(node, false)
                                  .setSubTaskGroupType(
                                      UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
                            }
                          })
                      .build());
            } else {
              // Only disk resizing, could be done without restarts.
              createNonRestartUpgradeTaskFlow(
                  (nodez, processTypes) ->
                      createUpdateDiskSizeTasks(nodez)
                          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ResizingDisk),
                  new ArrayList<>(nodes),
                  ServerType.EITHER,
                  DEFAULT_CONTEXT);
            }

            Integer newDiskSize = null;
            if (cluster.userIntent.deviceInfo != null) {
              newDiskSize = cluster.userIntent.deviceInfo.volumeSize;
            }
            // Persist changes in the universe.
            createPersistResizeNodeTask(
                    newInstanceType, newDiskSize, Collections.singletonList(cluster.uuid))
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
          }
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
      List<NodeDetails> nodes,
      Universe universe,
      boolean instanceTypeIsChanging,
      UniverseDefinitionTaskParams.Cluster cluster) {

    UniverseDefinitionTaskParams.UserIntent currUserIntent =
        universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;

    Integer currDiskSize = currUserIntent.deviceInfo.volumeSize;
    String currInstanceType = currUserIntent.instanceType;
    // Todo: Add preflight checks here

    // Change disk size.
    DeviceInfo deviceInfo = cluster.userIntent.deviceInfo;
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
    String newInstanceType = cluster.userIntent.instanceType;
    if (instanceTypeIsChanging) {
      for (NodeDetails node : nodes) {
        // Check if the node needs to be resized.
        if (!taskParams().isForceResizeNode()
            && node.cloudInfo.instance_type.equals(newInstanceType)) {
          log.info("Skipping node {} as its type is already {}", node.nodeName, currInstanceType);
          continue;
        }

        // Change the instance type.
        createChangeInstanceTypeTask(node, cluster.userIntent.instanceType)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
      }
    }
  }

  private SubTaskGroup createChangeInstanceTypeTask(NodeDetails node, String instanceType) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("ChangeInstanceType", executor);
    ChangeInstanceType.Params params = new ChangeInstanceType.Params();

    params.nodeName = node.nodeName;
    params.universeUUID = taskParams().universeUUID;
    params.azUuid = node.azUuid;
    params.instanceType = instanceType;

    ChangeInstanceType changeInstanceTypeTask = createTask(ChangeInstanceType.class);
    changeInstanceTypeTask.initialize(params);
    subTaskGroup.addSubTask(changeInstanceTypeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
