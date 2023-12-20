// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateNodeDetails;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public abstract class UpgradeTaskBase extends UniverseDefinitionTaskBase {
  protected static final UpgradeContext DEFAULT_CONTEXT =
      UpgradeContext.builder()
          .reconfigureMaster(false)
          .runBeforeStopping(false)
          .processInactiveMaster(false)
          .build();
  protected static final UpgradeContext RUN_BEFORE_STOPPING =
      UpgradeContext.builder()
          .reconfigureMaster(false)
          .runBeforeStopping(true)
          .processInactiveMaster(false)
          .build();

  // Variable to mark if the loadbalancer state was changed.
  protected boolean isLoadBalancerOn = true;
  protected boolean isBlacklistLeaders;
  protected int leaderBacklistWaitTimeMs;
  protected boolean hasRollingUpgrade = false;

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

  /** Similar to {@link #runUpgrade(Consumer, Consumer, Runnable)} without the other params. */
  public void runUpgrade(Runnable upgradeLambda) {
    checkUniverseVersion();
    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, null /* Txn callback */);
    try {
      isBlacklistLeaders =
          runtimeConfigFactory.forUniverse(universe).getBoolean(Util.BLACKLIST_LEADERS);
      leaderBacklistWaitTimeMs =
          runtimeConfigFactory.forUniverse(universe).getInt(Util.BLACKLIST_LEADER_WAIT_TIME_MS);
      // Execute the lambda which populates subTaskGroupQueue
      upgradeLambda.run();

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error={}.", getName(), t);

      // This clears all the previously added subtasks.
      getRunnableTask().reset();
      // If the task failed, we don't want the loadbalancer to be
      // disabled, so we enable it again in case of errors.
      if (!isLoadBalancerOn) {
        createLoadBalancerStateChangeTask(true)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
      getRunnableTask().runSubTasks();

      throw t;
    } finally {
      try {
        if (isBlacklistLeaders && hasRollingUpgrade) {
          // This clears all the previously added subtasks.
          getRunnableTask().reset();
          List<NodeDetails> tServerNodes = fetchTServerNodes(taskParams().upgradeOption);
          createModifyBlackListTask(tServerNodes, false /* isAdd */, true /* isLeaderBlacklist */)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          getRunnableTask().runSubTasks();
        }
      } finally {
        unlockUniverseForUpdate();
      }
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
        createNonRestartUpgradeTaskFlow(lambda, mastersAndTServers, context);
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

  /**
   * Used for full node upgrades (for example resize) where all processes are stopped.
   *
   * @param lambda - for performing upgrade actions
   * @param nodeSet - set of nodes sorted in appropriate order.
   */
  public void createRollingNodesUpgradeTaskFlow(
      IUpgradeSubTask lambda, LinkedHashSet<NodeDetails> nodeSet, UpgradeContext context) {
    createRollingUpgradeTaskFlow(
        lambda,
        nodeSet,
        nodeDetails -> {
          Set<ServerType> result = new LinkedHashSet<>();
          if (nodeDetails.isMaster) {
            result.add(ServerType.MASTER);
          }
          if (nodeDetails.isTserver) {
            result.add(ServerType.TSERVER);
          }
          return result;
        },
        context,
        true);
  }

  private void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Collection<NodeDetails> nodes,
      ServerType baseProcessType,
      UpgradeContext context,
      boolean activeRole) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda,
        nodes,
        node -> Collections.singleton(baseProcessType),
        context,
        activeRole);
  }

  private void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Collection<NodeDetails> nodes,
      Function<NodeDetails, Set<ServerType>> processTypesFunction,
      UpgradeContext context,
      boolean activeRole) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }
    hasRollingUpgrade = true;
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    Map<NodeDetails, Set<ServerType>> typesByNode = new HashMap<>();
    boolean hasTServer = false;
    for (NodeDetails node : nodes) {
      Set<ServerType> serverTypes = processTypesFunction.apply(node);
      hasTServer = hasTServer || serverTypes.contains(ServerType.TSERVER);
      typesByNode.put(node, serverTypes);
    }

    NodeState nodeState = getNodeState();

    // Need load balancer on to perform leader blacklist.
    if (hasTServer) {
      if (!isBlacklistLeaders) {
        createLoadBalancerStateChangeTask(false).setSubTaskGroupType(subGroupType);
        isLoadBalancerOn = false;
      } else {
        createModifyBlackListTask(nodes, false /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
      }
    }

    for (NodeDetails node : nodes) {
      Set<ServerType> processTypes = typesByNode.get(node);
      List<NodeDetails> singletonNodeList = Collections.singletonList(node);
      boolean isLeaderBlacklistValidRF = isLeaderBlacklistValidRF(node.nodeName);
      createSetNodeStateTask(node, nodeState).setSubTaskGroupType(subGroupType);
      if (context.runBeforeStopping) {
        rollingUpgradeLambda.run(singletonNodeList, processTypes);
      }
      // set leader blacklist and poll
      if (processTypes.contains(ServerType.TSERVER)
          && isBlacklistLeaders
          && isLeaderBlacklistValidRF) {
        createModifyBlackListTask(
                Collections.singletonList(node), true /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
        createWaitForLeaderBlacklistCompletionTask(leaderBacklistWaitTimeMs)
            .setSubTaskGroupType(subGroupType);
      }
      for (ServerType processType : processTypes) {
        createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
        if (processType == ServerType.MASTER && context.reconfigureMaster && activeRole) {
          createWaitForMasterLeaderTask().setSubTaskGroupType(subGroupType);
          createChangeConfigTask(node, false /* isAdd */, subGroupType);
        }
      }
      if (!context.runBeforeStopping) {
        rollingUpgradeLambda.run(singletonNodeList, processTypes);
      }
      if (activeRole) {
        for (ServerType processType : processTypes) {
          if (!context.skipStartingProcesses) {
            createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
          }
          createWaitForServersTasks(singletonNodeList, processType)
              .setSubTaskGroupType(subGroupType);
          if (processType.equals(ServerType.TSERVER) && node.isYsqlServer) {
            createWaitForServersTasks(singletonNodeList, ServerType.YSQLSERVER)
                .setSubTaskGroupType(subGroupType);
          }
          if (processType == ServerType.MASTER && context.reconfigureMaster) {
            // Add stopped master to the quorum.
            createChangeConfigTask(node, true /* isAdd */, subGroupType);
          }
          createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
              .setSubTaskGroupType(subGroupType);
        }
        createWaitForKeyInMemoryTask(node).setSubTaskGroupType(subGroupType);
      }

      // remove leader blacklist
      if (processTypes.contains(ServerType.TSERVER)
          && isBlacklistLeaders
          && isLeaderBlacklistValidRF) {
        createModifyBlackListTask(
                Collections.singletonList(node), false /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
      }
      if (activeRole) {
        for (ServerType processType : processTypes) {
          createWaitForFollowerLagTask(node, processType).setSubTaskGroupType(subGroupType);
        }
      }

      if (context.postAction != null) {
        context.postAction.accept(node);
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
      nonRollingUpgradeLambda.run(nodes, Collections.singleton(processType));
    }
    createServerControlTasks(nodes, processType, "stop").setSubTaskGroupType(subGroupType);
    if (!context.runBeforeStopping) {
      nonRollingUpgradeLambda.run(nodes, Collections.singleton(processType));
    }

    if (activeRole) {
      createServerControlTasks(nodes, processType, "start").setSubTaskGroupType(subGroupType);
      createWaitForServersTasks(nodes, processType)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
    if (context.postAction != null) {
      nodes.forEach(context.postAction);
    }

    createSetNodeStateTasks(nodes, NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  public void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      UpgradeContext context) {
    createNonRestartUpgradeTaskFlow(
        nonRestartUpgradeLambda,
        mastersAndTServers.getLeft(),
        mastersAndTServers.getRight(),
        context);
  }

  public void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context) {
    createNonRestartUpgradeTaskFlow(
        nonRestartUpgradeLambda, masterNodes, ServerType.MASTER, context);
    createNonRestartUpgradeTaskFlow(
        nonRestartUpgradeLambda, tServerNodes, ServerType.TSERVER, context);
  }

  protected void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda,
      List<NodeDetails> nodes,
      ServerType processType,
      UpgradeContext context) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();
    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);
    nonRestartUpgradeLambda.run(nodes, Collections.singleton(processType));
    if (context.postAction != null) {
      nodes.forEach(context.postAction);
    }
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
          (nodes, processType) -> {}, masterNodes, tServerNodes, DEFAULT_CONTEXT);
    } else {
      createNonRollingUpgradeTaskFlow(
          (nodes, processType) -> {}, masterNodes, tServerNodes, DEFAULT_CONTEXT);
    }
  }

  protected TaskExecutor.SubTaskGroup createNodeDetailsUpdateTask(
      NodeDetails node, boolean updateCustomImageUsage) {
    TaskExecutor.SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("UpdateNodeDetails", executor);
    UpdateNodeDetails.Params updateNodeDetailsParams = new UpdateNodeDetails.Params();
    updateNodeDetailsParams.universeUUID = taskParams().universeUUID;
    updateNodeDetailsParams.azUuid = node.azUuid;
    updateNodeDetailsParams.nodeName = node.nodeName;
    updateNodeDetailsParams.details = node;
    updateNodeDetailsParams.updateCustomImageUsage = updateCustomImageUsage;

    UpdateNodeDetails updateNodeTask = createTask(UpdateNodeDetails.class);
    updateNodeTask.initialize(updateNodeDetailsParams);
    updateNodeTask.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(updateNodeTask);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected ServerType getSingle(Set<ServerType> processTypes) {
    if (processTypes.size() != 1) {
      throw new IllegalArgumentException("Expected to have single element, got " + processTypes);
    }
    return processTypes.iterator().next();
  }

  private List<NodeDetails> filterForClusters(List<NodeDetails> nodes) {
    Set<UUID> clusterUUIDs =
        taskParams().clusters.stream().map(c -> c.uuid).collect(Collectors.toSet());
    return nodes
        .stream()
        .filter(n -> clusterUUIDs.contains(n.placementUuid))
        .collect(Collectors.toList());
  }

  public LinkedHashSet<NodeDetails> fetchNodesForCluster() {
    Universe universe = getUniverse();
    return toOrderedSet(
        new ImmutablePair<>(
            filterForClusters(fetchMasterNodes(universe, taskParams().upgradeOption)),
            filterForClusters(fetchTServerNodes(universe, taskParams().upgradeOption))));
  }

  protected LinkedHashSet<NodeDetails> toOrderedSet(
      Pair<List<NodeDetails>, List<NodeDetails>> nodes) {
    LinkedHashSet<NodeDetails> nodeSet = new LinkedHashSet<>();
    nodeSet.addAll(nodes.getLeft());
    nodeSet.addAll(nodes.getRight());
    return nodeSet;
  }

  public ImmutablePair<List<NodeDetails>, List<NodeDetails>> fetchNodes(
      UpgradeOption upgradeOption) {
    return new ImmutablePair<>(fetchMasterNodes(upgradeOption), fetchTServerNodes(upgradeOption));
  }

  public List<NodeDetails> fetchMasterNodes(UpgradeOption upgradeOption) {
    return fetchMasterNodes(getUniverse(), upgradeOption);
  }

  private List<NodeDetails> fetchMasterNodes(Universe universe, UpgradeOption upgradeOption) {
    List<NodeDetails> masterNodes = universe.getMasters();
    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      final String leaderMasterAddress = universe.getMasterLeaderHostText();
      return sortMastersInRestartOrder(leaderMasterAddress, masterNodes);
    }
    return masterNodes;
  }

  public List<NodeDetails> fetchTServerNodes(UpgradeOption upgradeOption) {
    return fetchTServerNodes(getUniverse(), upgradeOption);
  }

  private List<NodeDetails> fetchTServerNodes(Universe universe, UpgradeOption upgradeOption) {
    List<NodeDetails> tServerNodes = universe.getTServers();
    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      return sortTServersInRestartOrder(universe, tServerNodes);
    }
    return tServerNodes;
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
    boolean reconfigureMaster;
    boolean runBeforeStopping;
    boolean processInactiveMaster;
    @Builder.Default boolean skipStartingProcesses = false;
    Consumer<NodeDetails> postAction;
  }
}
