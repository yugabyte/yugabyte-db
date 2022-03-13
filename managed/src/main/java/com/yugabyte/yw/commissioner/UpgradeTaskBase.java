// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public abstract class UpgradeTaskBase extends UniverseDefinitionTaskBase {
  protected static final UpgradeContext DEFAULT_CONTEXT =
      UpgradeContext.builder().runBeforeStopping(false).processInactiveMaster(false).build();
  protected static final UpgradeContext RUN_BEFORE_STOPPING =
      UpgradeContext.builder().runBeforeStopping(true).processInactiveMaster(false).build();

  // Variable to mark if the loadbalancer state was changed.
  protected boolean isLoadBalancerOn = true;
  protected boolean isBlacklistLeaders;
  protected int leaderBacklistWaitTimeMs;

  protected UpgradeTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected UpgradeTaskParams taskParams() {
    return (UpgradeTaskParams) taskParams;
  }

  public abstract SubTaskGroupType getTaskSubGroupType();

  // State set on node while it is being upgraded
  public abstract NodeState getNodeState();

  // Wrapper that takes care of common pre and post upgrade tasks and user has
  // flexibility to manipulate subTaskGroupQueue through the lambda passed in parameter
  public void runUpgrade(IUpgradeTaskWrapper upgradeLambda) {
    try {
      isBlacklistLeaders =
          runtimeConfigFactory.forUniverse(getUniverse()).getBoolean(Util.BLACKLIST_LEADERS);
      leaderBacklistWaitTimeMs =
          runtimeConfigFactory
              .forUniverse(getUniverse())
              .getInt(Util.BLACKLIST_LEADER_WAIT_TIME_MS);
      checkUniverseVersion();
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Execute the lambda which populates subTaskGroupQueue
      upgradeLambda.run();

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error={}.", getName(), t);

      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
      // If the task failed, we don't want the loadbalancer to be
      // disabled, so we enable it again in case of errors.
      if (!isLoadBalancerOn) {
        createLoadBalancerStateChangeTask(true)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
      subTaskGroupQueue.run();

      throw t;
    } finally {
      if (isBlacklistLeaders) {
        subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
        List<NodeDetails> tServerNodes = fetchTServerNodes(taskParams().upgradeOption);
        createModifyBlackListTask(tServerNodes, false /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        subTaskGroupQueue.run();
      }
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }

  public void createUpgradeTaskFlow(
      IUpgradeSubTask lambda,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      UpgradeContext context) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(lambda, mastersAndTServers, context);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(lambda, mastersAndTServers, context);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(lambda, mastersAndTServers);
        break;
    }
  }

  public void createUpgradeTaskFlow(
      IUpgradeSubTask lambda,
      UpgradeOption upgradeOption,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context) {
    switch (upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(lambda, masterNodes, tServerNodes, context);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(lambda, masterNodes, tServerNodes, context);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(lambda, masterNodes, tServerNodes);
        break;
    }
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      UpgradeContext context) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda, mastersAndTServers.getLeft(), mastersAndTServers.getRight(), context);
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda, masterNodes, ServerType.MASTER, context, true);
    if (context.processInactiveMaster) {
      createRollingUpgradeTaskFlow(
          rollingUpgradeLambda,
          getInactiveMasters(masterNodes, tServerNodes),
          ServerType.MASTER,
          context,
          false);
    }
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda, tServerNodes, ServerType.TSERVER, context, true);
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      List<NodeDetails> nodes,
      ServerType processType,
      UpgradeContext context,
      boolean activeRole) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();

    // Need load balancer on to perform leader blacklist.
    if (processType == ServerType.TSERVER) {
      if (!isBlacklistLeaders) {
        createLoadBalancerStateChangeTask(false).setSubTaskGroupType(subGroupType);
        isLoadBalancerOn = false;
      } else {
        createModifyBlackListTask(nodes, false /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
      }
    }

    for (NodeDetails node : nodes) {
      List<NodeDetails> singletonNodeList = Collections.singletonList(node);
      boolean isLeaderBlacklistValidRF = isLeaderBlacklistValidRF(node.nodeName);
      createSetNodeStateTask(node, nodeState).setSubTaskGroupType(subGroupType);
      if (context.runBeforeStopping) {
        rollingUpgradeLambda.run(singletonNodeList, processType);
      }
      // set leader blacklist and poll
      if (processType == ServerType.TSERVER && isBlacklistLeaders && isLeaderBlacklistValidRF) {
        createModifyBlackListTask(
                Arrays.asList(node), true /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
        createWaitForLeaderBlacklistCompletionTask(leaderBacklistWaitTimeMs)
            .setSubTaskGroupType(subGroupType);
      }
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
      if (!context.runBeforeStopping) {
        rollingUpgradeLambda.run(singletonNodeList, processType);
      }
      if (activeRole) {
        createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
        createWaitForServersTasks(singletonNodeList, processType).setSubTaskGroupType(subGroupType);
        createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
            .setSubTaskGroupType(subGroupType);
        createWaitForKeyInMemoryTask(node).setSubTaskGroupType(subGroupType);
      }

      // remove leader blacklist
      if (processType == ServerType.TSERVER && isBlacklistLeaders && isLeaderBlacklistValidRF) {
        createModifyBlackListTask(
                Arrays.asList(node), false /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
      }
      createSetNodeStateTask(node, NodeState.Live).setSubTaskGroupType(subGroupType);
    }

    if (!isLoadBalancerOn) {
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(subGroupType);
      isLoadBalancerOn = true;
    }
  }

  public void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      UpgradeContext context) {
    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda,
        mastersAndTServers.getLeft(),
        mastersAndTServers.getRight(),
        context);
  }

  public void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context) {

    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda, masterNodes, ServerType.MASTER, context, true);

    if (context.processInactiveMaster) {
      createNonRollingUpgradeTaskFlow(
          nonRollingUpgradeLambda,
          getInactiveMasters(masterNodes, tServerNodes),
          ServerType.MASTER,
          context,
          false);
    }
    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda, tServerNodes, ServerType.TSERVER, context, true);
  }

  private List<NodeDetails> getInactiveMasters(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    Universe universe = getUniverse();
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    return tServerNodes != null
        ? tServerNodes
            .stream()
            .filter(node -> node.placementUuid.equals(primaryClusterUuid))
            .filter(node -> masterNodes == null || !masterNodes.contains(node))
            .collect(Collectors.toList())
        : null;
  }

  private void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      List<NodeDetails> nodes,
      ServerType processType,
      UpgradeContext context,
      boolean activeRole) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();

    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);
    if (context.runBeforeStopping) {
      nonRollingUpgradeLambda.run(nodes, processType);
    }
    createServerControlTasks(nodes, processType, "stop").setSubTaskGroupType(subGroupType);
    if (!context.runBeforeStopping) {
      nonRollingUpgradeLambda.run(nodes, processType);
    }

    if (activeRole) {
      createServerControlTasks(nodes, processType, "start").setSubTaskGroupType(subGroupType);
      createWaitForServersTasks(nodes, processType)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    createSetNodeStateTasks(nodes, NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  public void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers) {
    createNonRestartUpgradeTaskFlow(
        nonRestartUpgradeLambda, mastersAndTServers.getLeft(), mastersAndTServers.getRight());
  }

  public void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes) {
    createNonRestartUpgradeTaskFlow(nonRestartUpgradeLambda, masterNodes, ServerType.MASTER);
    createNonRestartUpgradeTaskFlow(nonRestartUpgradeLambda, tServerNodes, ServerType.TSERVER);
  }

  private void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda, List<NodeDetails> nodes, ServerType processType) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();
    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);
    nonRestartUpgradeLambda.run(nodes, processType);
    createSetNodeStateTasks(nodes, NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  public void createRestartTasks(
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers, UpgradeOption upgradeOption) {
    createRestartTasks(mastersAndTServers.getLeft(), mastersAndTServers.getRight(), upgradeOption);
  }

  private void createRestartTasks(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes, UpgradeOption upgradeOption) {
    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE
        && upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      throw new IllegalArgumentException("Restart can only be either rolling or non-rolling");
    }

    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      createRollingUpgradeTaskFlow(
          (List<NodeDetails> nodes, ServerType processType) -> {},
          masterNodes,
          tServerNodes,
          DEFAULT_CONTEXT);
    } else {
      createNonRollingUpgradeTaskFlow(
          (List<NodeDetails> nodes, ServerType processType) -> {},
          masterNodes,
          tServerNodes,
          DEFAULT_CONTEXT);
    }
  }

  public ImmutablePair<List<NodeDetails>, List<NodeDetails>> fetchNodes(
      UpgradeOption upgradeOption) {
    return new ImmutablePair<>(fetchMasterNodes(upgradeOption), fetchTServerNodes(upgradeOption));
  }

  public List<NodeDetails> fetchMasterNodes(UpgradeOption upgradeOption) {
    List<NodeDetails> masterNodes = getUniverse().getMasters();
    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      final String leaderMasterAddress = getUniverse().getMasterLeaderHostText();
      return sortMastersInRestartOrder(leaderMasterAddress, masterNodes);
    }
    return masterNodes;
  }

  public List<NodeDetails> fetchTServerNodes(UpgradeOption upgradeOption) {
    List<NodeDetails> tServerNodes = getUniverse().getTServers();
    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      return sortTServersInRestartOrder(getUniverse(), tServerNodes);
    }
    return tServerNodes;
  }

  public AnsibleConfigureServers.Params getAnsibleConfigureServerParams(
      NodeDetails node,
      ServerType processType,
      UpgradeTaskType type,
      UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    UserIntent userIntent =
        getUniverse().getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent;
    // Set the device information (numVolumes, volumeSize, etc.)
    params.deviceInfo = userIntent.deviceInfo;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // Add in the node placement uuid.
    params.placementUuid = node.placementUuid;
    // Add testing flag.
    params.itestS3PackagePath = taskParams().itestS3PackagePath;
    // Add task type
    params.type = type;
    params.setProperty("processType", processType.toString());
    params.setProperty("taskSubType", taskSubType.toString());

    if (userIntent.providerType.equals(CloudType.onprem)) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    return params;
  }

  public int getSleepTimeForProcess(ServerType processType) {
    return processType == ServerType.MASTER
        ? taskParams().sleepAfterMasterRestartMillis
        : taskParams().sleepAfterTServerRestartMillis;
  }

  // Find the master leader and move it to the end of the list.
  private List<NodeDetails> sortMastersInRestartOrder(
      String leaderMasterAddress, List<NodeDetails> nodes) {
    if (nodes.isEmpty()) {
      return nodes;
    }
    return nodes
        .stream()
        .sorted(
            Comparator.<NodeDetails, Boolean>comparing(
                    node -> leaderMasterAddress.equals(node.cloudInfo.private_ip))
                .thenComparing(NodeDetails::getNodeIdx))
        .collect(Collectors.toList());
  }

  // Find the master leader and move it to the end of the list.
  private List<NodeDetails> sortTServersInRestartOrder(Universe universe, List<NodeDetails> nodes) {
    if (nodes.isEmpty()) {
      return nodes;
    }

    Map<UUID, Map<UUID, PlacementAZ>> placementAZMapPerCluster =
        PlacementInfoUtil.getPlacementAZMapPerCluster(universe);
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    return nodes
        .stream()
        .sorted(
            Comparator.<NodeDetails, Boolean>comparing(
                    // Fully upgrade primary cluster first
                    node -> !node.placementUuid.equals(primaryClusterUuid))
                .thenComparing(
                    node -> {
                      Map<UUID, PlacementAZ> placementAZMap =
                          placementAZMapPerCluster.get(node.placementUuid);
                      if (placementAZMap == null) {
                        // Well, this shouldn't happen
                        // but just to make sure we'll not fail - sort to the end
                        log.warn("placementAZMap is null for cluster: " + node.placementUuid);
                        return true;
                      }
                      PlacementAZ placementAZ = placementAZMap.get(node.azUuid);
                      if (placementAZ == null) {
                        return true;
                      }
                      // Primary zones go first
                      return !placementAZ.isAffinitized;
                    })
                .thenComparing(NodeDetails::getNodeIdx))
        .collect(Collectors.toList());
  }

  @Value
  @Builder
  protected static class UpgradeContext {
    boolean runBeforeStopping;
    boolean processInactiveMaster;
  }
}
