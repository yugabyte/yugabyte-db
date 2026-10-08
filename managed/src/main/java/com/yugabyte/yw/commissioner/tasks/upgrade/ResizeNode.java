// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
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
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@CanRollback
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
    verifyResizeParams(isFirstTry);
  }

  /**
   * Forward ResizeNode verifies {@link ResizeNodeParams}. Rollback overrides this so the generic
   * universe checks still run without applying forward-only {@code verifyParams}.
   */
  protected void verifyResizeParams(boolean isFirstTry) {
    taskParams().verifyParams(getUniverse(), !isFirstTry() ? getNodeState() : null, isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
    if (confGetter.getGlobalConf(GlobalConfKeys.ociFailFastMultiVolumeInstanceTypeChange)) {
      validateOciInstanceTypeChangeVolumes(universe);
    }
  }

  private void validateOciInstanceTypeChangeVolumes(Universe universe) {
    Function<NodeDetails, Provider> providerGetter = Util.getProviderGetter(universe);
    for (Cluster cluster : taskParams().clusters) {
      Cluster currentCluster = universe.getCluster(cluster.uuid);
      if (currentCluster == null) {
        continue;
      }
      UserIntent currentIntent = currentCluster.userIntent;
      UserIntent newIntent = cluster.userIntent;
      for (NodeDetails node : universe.getNodesInCluster(cluster.uuid)) {
        if (providerGetter.apply(node).getCloudCode() != Common.CloudType.oci) {
          continue;
        }
        if (Objects.equals(
            newIntent.getInstanceTypeForNode(node), currentIntent.getInstanceTypeForNode(node))) {
          continue;
        }
        DeviceInfo deviceInfo = currentIntent.evaluateDeviceInfoForNode(node);
        if (deviceInfo != null && deviceInfo.numVolumes != null && deviceInfo.numVolumes > 1) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              String.format(
                  "Cannot change instance type on OCI when more than one data volume is attached"
                      + " (found %d). OCI allows at most one boot volume and one secondary volume.",
                  deviceInfo.numVolumes));
        }
      }
    }
  }

  @Override
  protected boolean isSkipPrechecks() {
    NodesToApply nodesToApply = calclulateNodesToApply();
    if (!nodesToApply.applyGFlagsToAllNodes
        && nodesToApply.instanceChangingNodes.isEmpty()
        && nodesToApply.tserversToUpgradeGFlags.isEmpty()
        && nodesToApply.mastersToUpgradeGFlags.isEmpty()
        && !nodesToApply.justModifyDeviceNodes.isEmpty()) {
      return true;
    }
    return super.isSkipPrechecks();
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
    final Map<UUID, UniverseDefinitionTaskParams.Cluster> gflagsBaseline =
        flagsProvided ? getGFlagsBaselineClusters(universe) : Collections.emptyMap();
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
        } else if (taskParams().isForceResizeNode()
            || isModifyingDevice(
                node,
                currentIntent.evaluateDeviceInfoForNode(node),
                userIntent.evaluateDeviceInfoForNode(node))) {
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
                        gflagsBaseline.get(n.placementUuid);
                    UniverseDefinitionTaskParams.Cluster newCluster =
                        newVersionsOfClusters.get(n.placementUuid);
                    return nodesToApplyCalculated.applyGFlagsToAllNodes
                        || GFlagsUpgradeParams.nodeHasGflagsChanges(
                            n,
                            ServerType.MASTER,
                            curCluster,
                            gflagsBaseline.values(),
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
                        gflagsBaseline.get(n.placementUuid);
                    UniverseDefinitionTaskParams.Cluster newCluster =
                        newVersionsOfClusters.get(n.placementUuid);
                    return nodesToApplyCalculated.applyGFlagsToAllNodes
                        || GFlagsUpgradeParams.nodeHasGflagsChanges(
                            n,
                            ServerType.TSERVER,
                            curCluster,
                            gflagsBaseline.values(),
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

          boolean deleteCapacityReservation =
              createCapacityReservationsIfNeeded(
                  nodesToApply.instanceChangingNodes,
                  CapacityReservationUtil.OperationType.RESIZE,
                  node -> {
                    UniverseDefinitionTaskParams.Cluster targetCluster =
                        taskParams().getClusterByUuid(node.placementUuid);
                    String targetInstanceType =
                        targetCluster.userIntent.getInstanceTypeForNode(node);
                    return !node.cloudInfo.instance_type.equals(targetInstanceType);
                  });

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
                    .nodesAreStopped(true)
                    .postAction(
                        node -> {
                          // Persist the new instance type in the node details.
                          String instanceType = userIntent.getInstanceTypeForNode(node);
                          node.cloudInfo.instance_type = instanceType;
                          createUpdateUniverseFieldsTask(
                                  univ -> {
                                    NodeDetails nodeDetails = univ.getNode(node.nodeName);
                                    if (nodeDetails != null) {
                                      nodeDetails.cloudInfo.instance_type = instanceType;
                                    }
                                  })
                              .setSubTaskGroupType(
                                  UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
                        })
                    .build(),
                taskParams().isYbcInstalled());
            // Only disk modification, could be done without restarts. Volume grow is
            // irreversible in the cloud; flip rollbackSafe before the first size Disk_Update.
            if (anyNodeModifyingVolumeSize(justModifyDeviceNodes, userIntent, currentIntent)) {
              createMarkRollbackUnsafeTaskOnce();
            }
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
          if (deleteCapacityReservation) {
            createDeleteCapacityReservationTask();
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

    Map<UUID, UniverseDefinitionTaskParams.Cluster> gflagsBaseline =
        getGFlagsBaselineClusters(universe);

    for (NodeDetails node : nodes) {
      UUID clusterUUID = node.placementUuid;
      UniverseDefinitionTaskParams.Cluster oldCluster = gflagsBaseline.get(clusterUUID);
      UniverseDefinitionTaskParams.Cluster newCluster = newVersionsOfClusters.get(clusterUUID);

      for (ServerType processType : processTypes) {
        if (applyToAll
            || GFlagsUpgradeParams.nodeHasGflagsChanges(
                node,
                processType,
                oldCluster,
                gflagsBaseline.values(),
                newCluster,
                newVersionsOfClusters.values())) {
          createServerConfFileUpdateTasks(
              userIntentForFlags,
              nodes,
              Collections.singleton(processType),
              oldCluster,
              gflagsBaseline.values(),
              newCluster,
              newVersionsOfClusters.values());
        }
      }
    }
  }

  protected boolean isInstanceChanging(
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

  protected boolean isModifyingDevice(
      NodeDetails node, DeviceInfo currentDeviceInfo, DeviceInfo newDeviceInfo) {
    // Disk will not be modified if the cluster has no currently defined device info.
    if (currentDeviceInfo == null) {
      log.warn("Cannot modify disk since the cluster has no defined device info");
      return false;
    }
    if (newDeviceInfo == null) {
      return false;
    }
    boolean modifySize = isModifyingVolumeSize(currentDeviceInfo, newDeviceInfo);
    boolean modifyIops =
        newDeviceInfo.diskIops != null
            && !newDeviceInfo.diskIops.equals(currentDeviceInfo.diskIops);
    boolean modifyThroughput =
        newDeviceInfo.throughput != null
            && !newDeviceInfo.throughput.equals(currentDeviceInfo.throughput);
    return modifySize || modifyIops || modifyThroughput;
  }

  /**
   * True when {@code volumeSize} differs. Narrower than {@link #isModifyingDevice}: IOPS /
   * throughput-only changes stay rollbackable (subject to cooldown). Cloud volume grow cannot be
   * undone, so callers enqueue {@link #createMarkRollbackUnsafeTaskOnce()} before size Disk_Update.
   */
  private boolean isModifyingVolumeSize(DeviceInfo currentDeviceInfo, DeviceInfo newDeviceInfo) {
    return currentDeviceInfo != null
        && newDeviceInfo != null
        && newDeviceInfo.volumeSize != null
        && !newDeviceInfo.volumeSize.equals(currentDeviceInfo.volumeSize);
  }

  private boolean anyNodeModifyingVolumeSize(
      Collection<NodeDetails> nodes,
      UniverseDefinitionTaskParams.UserIntent newIntent,
      UniverseDefinitionTaskParams.UserIntent currentIntent) {
    return nodes.stream()
        .anyMatch(
            n ->
                isModifyingVolumeSize(
                    currentIntent.evaluateDeviceInfoForNode(n),
                    newIntent.evaluateDeviceInfoForNode(n)));
  }

  private void createPreResizeNodeTasks(
      Collection<NodeDetails> nodes, UniverseDefinitionTaskParams.UserIntent currentIntent) {
    // Update mounted disks.
    for (NodeDetails node : nodes) {
      if (!node.disksAreMountedByUUID) {
        createUpdateMountedDisksTask(
                node, node.getInstanceType(), currentIntent.evaluateDeviceInfoForNode(node))
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
            DeviceInfo newDeviceInfo = newIntent.evaluateDeviceInfoForNode(node);
            String newInstanceType = newIntent.getInstanceType(type, node.getAzUuid());
            String currentInstanceType = instanceTypeForChangeDecision(node);
            DeviceInfo currentDeviceInfo = currentIntent.evaluateDeviceInfoForNode(node);
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
            && isNodeAlreadyAtInstanceType(node, newInstanceType)
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
      if (taskParams().isForceResizeNode()
          || nodes.stream().anyMatch(n -> isModifyingDevice(n, currentDeviceInfo, newDeviceInfo))) {
        // Volume grow cannot be undone; flip rollbackSafe before the first size Disk_Update.
        // IOPS/throughput-only Disk_Update stays inside the rollback-safe window.
        if (isModifyingVolumeSize(currentDeviceInfo, newDeviceInfo)) {
          createMarkRollbackUnsafeTaskOnce();
        }
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

  /**
   * Instance type used to decide whether ChangeInstanceType must run. Forward ResizeNode uses YBA
   * postgres; rollback uses the cloud type when known so a persist-abort after the cloud change is
   * not skipped.
   */
  protected String instanceTypeForChangeDecision(NodeDetails node) {
    return node.cloudInfo.instance_type;
  }

  /**
   * True when YBA's recorded instance type already equals the target. Rollback overrides this to
   * also require the cloud instance type to match (persist may have aborted after
   * ChangeInstanceType).
   */
  protected boolean isNodeAlreadyAtInstanceType(NodeDetails node, String targetInstanceType) {
    return node.cloudInfo.instance_type.equals(targetInstanceType);
  }

  /**
   * Clusters used as the "old" side of gflag diffs and conf rewrites. Forward ResizeNode uses the
   * persisted universe. Rollback overrides this with the failed task's target so nodes the forward
   * already updated still get their conf rewritten back to before.
   */
  protected Map<UUID, UniverseDefinitionTaskParams.Cluster> getGFlagsBaselineClusters(
      Universe universe) {
    return universe.getUniverseDetails().clusters.stream()
        .collect(Collectors.toMap(c -> c.uuid, c -> c));
  }

  /**
   * Records the gflags this resize applies into the freeze-captured target. Legacy top-level {@code
   * masterGFlags}/{@code tserverGFlags} are not cluster fields, so the generic target would drop
   * them; RollbackResizeNode diffs against what is stored here.
   */
  @Override
  protected UniverseDefinitionTaskParams getTargetUniverseDetails() {
    UniverseDefinitionTaskParams target = super.getTargetUniverseDetails();
    Universe universe = getUniverse();
    if (target == null || !taskParams().flagsProvided(universe)) {
      return target;
    }
    Map<UUID, Cluster> newVersions = taskParams().getNewVersionsOfClusters(universe);
    for (Cluster cluster : target.clusters) {
      Cluster newVersion = newVersions.get(cluster.uuid);
      if (newVersion != null) {
        cluster.userIntent.specificGFlags = newVersion.userIntent.specificGFlags;
        cluster.userIntent.masterGFlags = newVersion.userIntent.masterGFlags;
        cluster.userIntent.tserverGFlags = newVersion.userIntent.tserverGFlags;
      }
    }
    return target;
  }

  private SubTaskGroup createChangeInstanceTypeTask(NodeDetails node, String instanceType) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ChangeInstanceType");
    ChangeInstanceType.Params params = new ChangeInstanceType.Params();
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Cluster nodeCluster = universe.getCluster(node.placementUuid);
    params.nodeName = node.nodeName;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.azUuid = node.azUuid;
    params.instanceType = instanceType;
    params.force = taskParams().isForceResizeNode();
    params.useSystemd = universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;
    params.placementUuid = node.placementUuid;
    params.cgroupSize = getCGroupSize(node);
    Common.CloudType providerType = nodeCluster.getProviderCloudType(node);
    params.skipAnsiblePlaybookForCGroup = NodeAgentClient.isCloudTypeSupported(providerType);
    ChangeInstanceType changeInstanceTypeTask = createTask(ChangeInstanceType.class);
    changeInstanceTypeTask.initialize(params);
    subTaskGroup.addSubTask(changeInstanceTypeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
