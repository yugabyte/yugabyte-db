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
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
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
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          // Verify the request params and fail if invalid.
          taskParams().verifyParams(universe);
          LinkedHashSet<NodeDetails> allNodes = fetchNodesForCluster();
          // Create task sequence to resize allNodes.
          for (UniverseDefinitionTaskParams.Cluster cluster : taskParams().clusters) {
            LinkedHashSet<NodeDetails> clusterNodes =
                allNodes
                    .stream()
                    .filter(n -> cluster.uuid.equals(n.placementUuid))
                    .collect(Collectors.toCollection(LinkedHashSet::new));

            final UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;

            UniverseDefinitionTaskParams.UserIntent currentIntent =
                universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;

            List<NodeDetails> justDeviceResizeNodes = new ArrayList<>();
            LinkedHashSet<NodeDetails> instanceChangingNodes = new LinkedHashSet<>();
            for (NodeDetails node : clusterNodes) {
              if (isInstanceChanging(node, userIntent)) {
                instanceChangingNodes.add(node);
              } else if (isDeviceResizing(node, userIntent, currentIntent)) {
                justDeviceResizeNodes.add(node);
              }
            }
            createPreResizeNodeTasks(instanceChangingNodes, currentIntent);
            createRollingNodesUpgradeTaskFlow(
                (nodes, processTypes) -> createResizeNodeTasks(nodes, userIntent, currentIntent),
                instanceChangingNodes,
                UpgradeContext.builder()
                    .reconfigureMaster(userIntent.replicationFactor > 1)
                    .runBeforeStopping(false)
                    .processInactiveMaster(false)
                    .postAction(
                        node -> {
                          // Persist the new instance type in the node details.
                          node.cloudInfo.instance_type = userIntent.getInstanceTypeForNode(node);
                          createNodeDetailsUpdateTask(node, false)
                              .setSubTaskGroupType(
                                  UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
                        })
                    .build(),
                taskParams().ybcInstalled);
            // Only disk resizing, could be done without restarts.
            createNonRestartUpgradeTaskFlow(
                (nodes, processTypes) ->
                    createUpdateDiskSizeTasks(nodes)
                        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ResizingDisk),
                justDeviceResizeNodes,
                ServerType.EITHER,
                DEFAULT_CONTEXT);

            Integer newDiskSize = null;
            if (userIntent.deviceInfo != null) {
              newDiskSize = userIntent.deviceInfo.volumeSize;
            }
            Integer newMasterDiskSize = null;
            if (userIntent.masterDeviceInfo != null) {
              newMasterDiskSize = userIntent.masterDeviceInfo.volumeSize;
            }
            String newInstanceType = userIntent.instanceType;
            String newMasterInstanceType = userIntent.masterInstanceType;
            // Persist changes in the universe.
            createPersistResizeNodeTask(
                    newInstanceType,
                    newDiskSize,
                    newMasterInstanceType,
                    newMasterDiskSize,
                    Collections.singletonList(cluster.uuid))
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
          }
        });
  }

  private boolean isInstanceChanging(
      NodeDetails node, UniverseDefinitionTaskParams.UserIntent newIntent) {
    if (taskParams().isForceResizeNode()) {
      return true;
    }
    String currentInstanceType = node.cloudInfo.instance_type;
    return !currentInstanceType.equals(newIntent.getInstanceTypeForNode(node));
  }

  private boolean isDeviceResizing(
      NodeDetails node,
      UniverseDefinitionTaskParams.UserIntent newIntent,
      UniverseDefinitionTaskParams.UserIntent currentIntent) {
    if (taskParams().isForceResizeNode()) {
      return true;
    }
    DeviceInfo currentDeviceInfo = currentIntent.getDeviceInfoForNode(node);
    DeviceInfo newDeviceInfo = newIntent.getDeviceInfoForNode(node);
    return !currentDeviceInfo.volumeSize.equals(newDeviceInfo.volumeSize);
  }

  private void createPreResizeNodeTasks(
      Collection<NodeDetails> nodes, UniverseDefinitionTaskParams.UserIntent currentIntent) {
    // Update mounted disks.
    for (NodeDetails node : nodes) {
      if (!node.disksAreMountedByUUID) {
        createUpdateMountedDisksTask(
                node,
                currentIntent.getInstanceTypeForNode(node),
                currentIntent.getDeviceInfoForNode(node))
            .setSubTaskGroupType(getTaskSubGroupType());
      }
    }
  }

  private void createResizeNodeTasks(
      List<NodeDetails> allNodes,
      UniverseDefinitionTaskParams.UserIntent newIntent,
      UniverseDefinitionTaskParams.UserIntent currentIntent) {
    Map<ServerType, List<NodeDetails>> byServerType = new HashMap<>();
    if (currentIntent.dedicatedNodes) {
      byServerType = allNodes.stream().collect(Collectors.groupingBy(n -> n.dedicatedTo));
    } else {
      byServerType.put(ServerType.EITHER, allNodes);
    }
    byServerType.forEach(
        (type, nodes) -> {
          String newInstanceType = newIntent.getInstanceTypeForProcessType(type);
          DeviceInfo newDeviceInfo = newIntent.getDeviceInfoForProcessType(type);
          String currentInstanceType = currentIntent.getInstanceTypeForProcessType(type);
          DeviceInfo currentDeviceInfo = currentIntent.getDeviceInfoForProcessType(type);
          createResizeNodeTasks(
              nodes, newInstanceType, newDeviceInfo, currentInstanceType, currentDeviceInfo);
        });
  }

  /**
   * @param nodes list of nodes that are guaranteed to have the same instance type and device.
   * @param newInstanceType
   * @param newDeviceInfo
   * @param currentInstanceType
   * @param currentDeviceInfo
   */
  private void createResizeNodeTasks(
      List<NodeDetails> nodes,
      String newInstanceType,
      DeviceInfo newDeviceInfo,
      String currentInstanceType,
      DeviceInfo currentDeviceInfo) {
    Integer currDiskSize = currentDeviceInfo.volumeSize;
    // Todo: Add preflight checks here

    // Change disk size.
    if (newDeviceInfo != null) {
      Integer newDiskSize = newDeviceInfo.volumeSize;
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
    if (!newInstanceType.equals(currentInstanceType) || taskParams().isForceResizeNode()) {
      for (NodeDetails node : nodes) {
        // Check if the node needs to be resized.
        if (!taskParams().isForceResizeNode()
            && node.cloudInfo.instance_type.equals(newInstanceType)) {
          log.info("Skipping node {} as its type is already {}", node.nodeName, newInstanceType);
          continue;
        }

        // Change the instance type.
        createChangeInstanceTypeTask(node, newInstanceType)
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
