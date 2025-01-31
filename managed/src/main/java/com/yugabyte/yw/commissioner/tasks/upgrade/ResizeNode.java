// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
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
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
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
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), !isFirstTry() ? getNodeState() : null, isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
  }

  private static class NodesToApply {
    LinkedHashSet<NodeDetails> instanceChangingNodes = new LinkedHashSet<>();
    List<NodeDetails> justModifyDeviceNodes = new ArrayList<>();
    List<NodeDetails> mastersToUpgradeGFlags = new ArrayList<>();
    List<NodeDetails> tserversToUpgradeGFlags = new ArrayList<>();
    boolean applyGFlagsToAllNodes = false;
  }

  private NodesToApply nodesToApplyCalculated;

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    NodesToApply nodesToApply = calclulateNodesToApply();
    List<NodeDetails> masters = new ArrayList<>(nodesToApply.mastersToUpgradeGFlags);
    List<NodeDetails> tservers = new ArrayList<>(nodesToApply.tserversToUpgradeGFlags);
    masters.addAll(nodesToApply.instanceChangingNodes);
    tservers.addAll(nodesToApply.instanceChangingNodes);
    return new MastersAndTservers(masters, tservers);
  }

  private NodesToApply calclulateNodesToApply() {
    if (nodesToApplyCalculated != null) {
      return nodesToApplyCalculated;
    }
    Universe universe = getUniverse();
    boolean flagsProvided = taskParams().flagsProvided(universe);
    UserIntent userIntentForFlags = getUserIntent();
    nodesToApplyCalculated = new NodesToApply();
    LinkedHashSet<NodeDetails> allNodes = fetchNodesForCluster();
    LinkedHashSet<NodeDetails> nodesNotUpdated = new LinkedHashSet<>(allNodes);
    Map<UUID, UniverseDefinitionTaskParams.Cluster> newVersionsOfClusters =
        taskParams().getNewVersionsOfClusters(universe);
    // Create task sequence to resize allNodes.
    for (UniverseDefinitionTaskParams.Cluster cluster : taskParams().clusters) {
      if (flagsProvided) {
        Map<String, String> masterGFlags =
            GFlagsUtil.getBaseGFlags(
                ServerType.MASTER,
                newVersionsOfClusters.get(cluster.uuid),
                newVersionsOfClusters.values());
        Map<String, String> tserverGFlags =
            GFlagsUtil.getBaseGFlags(
                ServerType.TSERVER,
                newVersionsOfClusters.get(cluster.uuid),
                newVersionsOfClusters.values());
        boolean updatedByMasterFlags =
            GFlagsUtil.syncGflagsToIntent(masterGFlags, userIntentForFlags);
        boolean updatedByTserverFlags =
            GFlagsUtil.syncGflagsToIntent(tserverGFlags, userIntentForFlags);
        nodesToApplyCalculated.applyGFlagsToAllNodes =
            updatedByMasterFlags || updatedByTserverFlags;
      }
      LinkedHashSet<NodeDetails> clusterNodes =
          allNodes.stream()
              .filter(n -> cluster.uuid.equals(n.placementUuid))
              .collect(Collectors.toCollection(LinkedHashSet::new));

      final UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;

      UniverseDefinitionTaskParams.UserIntent currentIntent =
          universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;

      for (NodeDetails node : clusterNodes) {
        if (isInstanceChanging(node, userIntent, currentIntent)) {
          nodesToApplyCalculated.instanceChangingNodes.add(node);
        } else if (isModifyingDevice(node, userIntent, currentIntent)) {
          nodesToApplyCalculated.justModifyDeviceNodes.add(node);
        }
      }
      // The nodes that are being resized will be restarted, so the gflags can be
      // upgraded in one go.
      nodesNotUpdated.removeAll(nodesToApplyCalculated.instanceChangingNodes);
    }
    // Need to run gflag upgrades for the nodes that weren't updated.
    if (flagsProvided) {
      nodesToApplyCalculated.mastersToUpgradeGFlags =
          nodesNotUpdated.stream()
              .filter(n -> n.isMaster)
              .filter(
                  n -> {
                    UniverseDefinitionTaskParams.Cluster curCluster =
                        universe.getCluster(n.placementUuid);
                    UniverseDefinitionTaskParams.Cluster newCluster =
                        newVersionsOfClusters.get(n.placementUuid);
                    return nodesToApplyCalculated.applyGFlagsToAllNodes
                        || GFlagsUpgradeParams.nodeHasGflagsChanges(
                            n,
                            ServerType.MASTER,
                            curCluster,
                            universe.getUniverseDetails().clusters,
                            newCluster,
                            newVersionsOfClusters.values());
                  })
              .collect(Collectors.toList());
      nodesToApplyCalculated.tserversToUpgradeGFlags =
          nodesNotUpdated.stream()
              .filter(n -> n.isTserver)
              .filter(
                  n -> {
                    UniverseDefinitionTaskParams.Cluster curCluster =
                        universe.getCluster(n.placementUuid);
                    UniverseDefinitionTaskParams.Cluster newCluster =
                        newVersionsOfClusters.get(n.placementUuid);
                    return nodesToApplyCalculated.applyGFlagsToAllNodes
                        || GFlagsUpgradeParams.nodeHasGflagsChanges(
                            n,
                            ServerType.TSERVER,
                            curCluster,
                            universe.getUniverseDetails().clusters,
                            newCluster,
                            newVersionsOfClusters.values());
                  })
              .collect(Collectors.toList());
    }
    return nodesToApplyCalculated;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          NodesToApply nodesToApply = calclulateNodesToApply();
          Universe universe = getUniverse();

          UserIntent userIntentForFlags = getUserIntent();

          boolean flagsProvided = taskParams().flagsProvided(universe);

          Map<UUID, UniverseDefinitionTaskParams.Cluster> newVersionsOfClusters =
              taskParams().getNewVersionsOfClusters(universe);
          AtomicBoolean applyGFlagsToAllNodes = new AtomicBoolean();
          // Create task sequence to resize allNodes.
          for (UniverseDefinitionTaskParams.Cluster cluster : taskParams().clusters) {
            final UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;

            UniverseDefinitionTaskParams.UserIntent currentIntent =
                universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;

            LinkedHashSet<NodeDetails> instanceChangingNodes =
                nodesToApply.instanceChangingNodes.stream()
                    .filter(n -> n.isInPlacement(cluster.uuid))
                    .collect(Collectors.toCollection(LinkedHashSet::new));

            List<NodeDetails> justModifyDeviceNodes =
                nodesToApply.justModifyDeviceNodes.stream()
                    .filter(n -> n.isInPlacement(cluster.uuid))
                    .collect(Collectors.toList());

            createPreResizeNodeTasks(instanceChangingNodes, currentIntent);
            createRollingNodesUpgradeTaskFlow(
                (nodes, processTypes) -> {
                  createResizeNodeTasks(nodes, userIntent, currentIntent);
                  if (flagsProvided) {
                    createGFlagsUpgradeTasks(
                        userIntentForFlags,
                        processTypes,
                        nodes,
                        universe,
                        newVersionsOfClusters,
                        applyGFlagsToAllNodes.get());
                  }
                },
                instanceChangingNodes,
                UpgradeContext.builder()
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
                taskParams().isYbcInstalled());
            // Only disk modification, could be done without restarts.
            createNonRestartUpgradeTaskFlow(
                (nodes, processTypes) ->
                    createUpdateDiskSizeTasks(nodes)
                        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ResizingDisk),
                justModifyDeviceNodes,
                ServerType.EITHER,
                DEFAULT_CONTEXT);
            // Persist changes in the universe.
            createPersistResizeNodeTask(userIntent, cluster.uuid)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
          }
          // Need to run gflag upgrades for the nodes that weren't updated.
          if (flagsProvided) {
            // Only rolling restart supported.
            createRollingUpgradeTaskFlow(
                (nodes, processTypes) ->
                    createGFlagsUpgradeTasks(
                        userIntentForFlags,
                        processTypes,
                        nodes,
                        universe,
                        newVersionsOfClusters,
                        applyGFlagsToAllNodes.get()),
                nodesToApply.mastersToUpgradeGFlags,
                nodesToApply.tserversToUpgradeGFlags,
                RUN_BEFORE_STOPPING,
                taskParams().isYbcInstalled());

            // Update the list of parameter key/values in the universe with the new ones.
            for (UniverseDefinitionTaskParams.Cluster cluster : taskParams().clusters) {
              updateGFlagsPersistTasks(
                      cluster,
                      taskParams().masterGFlags,
                      taskParams().tserverGFlags,
                      cluster.userIntent.specificGFlags)
                  .setSubTaskGroupType(getTaskSubGroupType());
            }
          }
        });
  }

  private void createGFlagsUpgradeTasks(
      UserIntent userIntentForFlags,
      Set<ServerType> processTypes,
      List<NodeDetails> nodes,
      Universe universe,
      Map<UUID, UniverseDefinitionTaskParams.Cluster> newVersionsOfClusters,
      boolean applyToAll) {

    for (NodeDetails node : nodes) {
      UUID clusterUUID = node.placementUuid;
      UniverseDefinitionTaskParams.Cluster oldCluster = universe.getCluster(clusterUUID);
      UniverseDefinitionTaskParams.Cluster newCluster = newVersionsOfClusters.get(clusterUUID);

      for (ServerType processType : processTypes) {
        if (applyToAll
            || GFlagsUpgradeParams.nodeHasGflagsChanges(
                node,
                processType,
                oldCluster,
                universe.getUniverseDetails().clusters,
                newCluster,
                newVersionsOfClusters.values())) {
          createServerConfFileUpdateTasks(
              userIntentForFlags,
              nodes,
              Collections.singleton(processType),
              oldCluster,
              universe.getUniverseDetails().clusters,
              newCluster,
              newVersionsOfClusters.values());
        }
      }
    }
  }

  private boolean isInstanceChanging(
      NodeDetails node,
      UniverseDefinitionTaskParams.UserIntent newIntent,
      UniverseDefinitionTaskParams.UserIntent currentIntent) {
    if (taskParams().isForceResizeNode()) {
      return true;
    }
    String currentInstanceType = node.cloudInfo.instance_type;
    Integer newCgroupSize = newIntent.getCGroupSize(node);
    Integer oldCgroupSize = currentIntent.getCGroupSize(node);

    return !currentInstanceType.equals(newIntent.getInstanceTypeForNode(node))
        || !Objects.equals(oldCgroupSize, newCgroupSize);
  }

  private boolean isModifyingDevice(
      NodeDetails node,
      UniverseDefinitionTaskParams.UserIntent newIntent,
      UniverseDefinitionTaskParams.UserIntent currentIntent) {
    if (taskParams().isForceResizeNode()) {
      return true;
    }
    DeviceInfo currentDeviceInfo = currentIntent.getDeviceInfoForNode(node);
    DeviceInfo newDeviceInfo = newIntent.getDeviceInfoForNode(node);
    return isModifyingDevice(currentDeviceInfo, newDeviceInfo);
  }

  private boolean isModifyingDevice(DeviceInfo currentDeviceInfo, DeviceInfo newDeviceInfo) {
    // Disk will not be modified if the cluster has no currently defined device info.
    if (currentDeviceInfo == null) {
      log.warn("Cannot modify disk since the cluster has no defined device info");
      return false;
    }
    boolean modifySize =
        newDeviceInfo.volumeSize != null
            && !newDeviceInfo.volumeSize.equals(currentDeviceInfo.volumeSize);
    boolean modifyIops =
        newDeviceInfo.diskIops != null
            && !newDeviceInfo.diskIops.equals(currentDeviceInfo.diskIops);
    boolean modifyThroughput =
        newDeviceInfo.throughput != null
            && !newDeviceInfo.throughput.equals(currentDeviceInfo.throughput);
    return modifySize || modifyIops || modifyThroughput;
  }

  private void createPreResizeNodeTasks(
      Collection<NodeDetails> nodes, UniverseDefinitionTaskParams.UserIntent currentIntent) {
    // Update mounted disks.
    for (NodeDetails node : nodes) {
      if (!node.disksAreMountedByUUID) {
        createUpdateMountedDisksTask(
                node, node.getInstanceType(), currentIntent.getDeviceInfoForNode(node))
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
          for (NodeDetails node : nodes) {
            DeviceInfo newDeviceInfo = newIntent.getDeviceInfoForNode(node);
            String newInstanceType = newIntent.getInstanceType(type, node.getAzUuid());
            String currentInstanceType = node.cloudInfo.instance_type;
            DeviceInfo currentDeviceInfo = currentIntent.getDeviceInfoForNode(node);
            Integer newCgroupSize = newIntent.getCGroupSize(node);
            Integer oldCgroupSize = currentIntent.getCGroupSize(node);
            createResizeNodeTasks(
                Collections.singletonList(node),
                newInstanceType,
                newDeviceInfo,
                currentInstanceType,
                currentDeviceInfo,
                !Objects.equals(oldCgroupSize, newCgroupSize));
          }
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
      DeviceInfo currentDeviceInfo,
      boolean cgroupSizeChanging) {
    // Todo: Add preflight checks here

    // Change instance type
    if (!newInstanceType.equals(currentInstanceType)
        || taskParams().isForceResizeNode()
        || cgroupSizeChanging) {
      for (NodeDetails node : nodes) {
        // Check if the node needs to be resized.
        if (!taskParams().isForceResizeNode()
            && node.cloudInfo.instance_type.equals(newInstanceType)
            && !cgroupSizeChanging) {
          log.info("Skipping node {} as its type is already {}", node.nodeName, newInstanceType);
          continue;
        }

        // Change the instance type.
        createChangeInstanceTypeTask(node, newInstanceType)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
      }
    }

    // Modify disk.
    log.info("Existing device info: {}", currentDeviceInfo);
    if (newDeviceInfo != null) {
      // Check if the storage needs to be modified.
      if (taskParams().isForceResizeNode() || isModifyingDevice(currentDeviceInfo, newDeviceInfo)) {
        // Resize the nodes' disks.
        log.info("New device info: {}", newDeviceInfo);
        createUpdateDiskSizeTasks(nodes, taskParams().isForceResizeNode())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ResizingDisk);
      } else {
        log.info(
            "No storage device properties were changed and forceResizeNode flag is false."
                + " Skipping disk modification.");
      }
    }
  }

  private SubTaskGroup createChangeInstanceTypeTask(NodeDetails node, String instanceType) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ChangeInstanceType");
    ChangeInstanceType.Params params = new ChangeInstanceType.Params();

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

    params.nodeName = node.nodeName;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.azUuid = node.azUuid;
    params.instanceType = instanceType;
    params.force = taskParams().isForceResizeNode();
    params.useSystemd = universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;
    params.placementUuid = node.placementUuid;
    params.cgroupSize = getCGroupSize(node);

    ChangeInstanceType changeInstanceTypeTask = createTask(ChangeInstanceType.class);
    changeInstanceTypeTask.initialize(params);
    subTaskGroup.addSubTask(changeInstanceTypeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
