// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateClusterUserIntent;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
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

  private List<ServerType> canBeIgnoredServerTypes = Arrays.asList(ServerType.CONTROLLER);

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
  protected boolean followerLagCheckEnabled;
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

  // Wrapper that takes care of common pre and post upgrade tasks and user has
  // flexibility to manipulate subTaskGroupQueue through the lambda passed in parameter
  public void runUpgrade(Runnable upgradeLambda) {
    try {
      isBlacklistLeaders =
          confGetter.getConfForScope(getUniverse(), UniverseConfKeys.ybUpgradeBlacklistLeaders);
      leaderBacklistWaitTimeMs =
          confGetter.getConfForScope(
              getUniverse(), UniverseConfKeys.ybUpgradeBlacklistLeaderWaitTimeMs);
      followerLagCheckEnabled =
          confGetter.getConfForScope(getUniverse(), UniverseConfKeys.followerLagCheckEnabled);
      checkUniverseVersion();
      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      Set<NodeDetails> nodeList = fetchAllNodes(taskParams().upgradeOption);

      // Run the pre-upgrade hooks
      createHookTriggerTasks(nodeList, true, false);

      // Execute the lambda which populates subTaskGroupQueue
      upgradeLambda.run();

      // Run the post-upgrade hooks
      createHookTriggerTasks(nodeList, false, false);

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
        try {
          unlockXClusterUniverses(lockedXClusterUniversesUuidSet, false /* ignoreErrors */);
        } finally {
          unlockUniverseForUpdate();
        }
      }
    }
    log.info("Finished {} task.", getName());
  }

  public void createUpgradeTaskFlow(
      IUpgradeSubTask lambda,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      UpgradeContext context,
      boolean isYbcPresent) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(lambda, mastersAndTServers, context, isYbcPresent);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(lambda, mastersAndTServers, context, isYbcPresent);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(lambda, mastersAndTServers, context);
        break;
    }
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      UpgradeContext context,
      boolean isYbcPresent) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda,
        mastersAndTServers.getLeft(),
        mastersAndTServers.getRight(),
        context,
        isYbcPresent);
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context,
      boolean isYbcPresent) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda, masterNodes, ServerType.MASTER, context, true, isYbcPresent);
    if (context.processInactiveMaster) {
      createRollingUpgradeTaskFlow(
          rollingUpgradeLambda,
          getInactiveMasters(masterNodes, tServerNodes),
          ServerType.MASTER,
          context,
          false,
          isYbcPresent);
    }
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda, tServerNodes, ServerType.TSERVER, context, true, isYbcPresent);
  }

  /**
   * Used for full node upgrades (for example resize) where all processes are stopped.
   *
   * @param lambda - for performing upgrade actions
   * @param nodeSet - set of nodes sorted in appropriate order.
   */
  public void createRollingNodesUpgradeTaskFlow(
      IUpgradeSubTask lambda,
      LinkedHashSet<NodeDetails> nodeSet,
      UpgradeContext context,
      boolean isYbcPresent) {
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
        true,
        isYbcPresent);
  }

  private void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Collection<NodeDetails> nodes,
      ServerType baseProcessType,
      UpgradeContext context,
      boolean activeRole,
      boolean isYbcPresent) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda,
        nodes,
        node -> new HashSet<>(Arrays.asList(baseProcessType)),
        context,
        activeRole,
        isYbcPresent);
  }

  private void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Collection<NodeDetails> nodes,
      Function<NodeDetails, Set<ServerType>> processTypesFunction,
      UpgradeContext context,
      boolean activeRole,
      boolean isYbcPresent) {
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

      createNodePrecheckTasks(node, processTypes, subGroupType, context.targetSoftwareVersion);

      // Run pre node upgrade hooks
      createHookTriggerTasks(singletonNodeList, true, true);
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

      if (isYbcPresent) {
        createServerControlTask(node, ServerType.CONTROLLER, "stop")
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
            createWaitForServersTasks(
                    singletonNodeList, ServerType.YSQLSERVER, context.getUserIntent())
                .setSubTaskGroupType(subGroupType);
          }

          if (processType == ServerType.MASTER && context.reconfigureMaster) {
            // Add stopped master to the quorum.
            createChangeConfigTasks(node, true /* isAdd */, subGroupType);
          }
          createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
              .setSubTaskGroupType(subGroupType);
          // If there are no universe keys on the universe, it will have no effect.
          if (processType == ServerType.MASTER
              && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
            createSetActiveUniverseKeysTask().setSubTaskGroupType(subGroupType);
          }
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
          if (followerLagCheckEnabled) {
            createCheckFollowerLagTask(node, processType).setSubTaskGroupType(subGroupType);
          }
        }
        if (isYbcPresent) {
          if (!context.skipStartingProcesses) {
            createServerControlTask(node, ServerType.CONTROLLER, "start")
                .setSubTaskGroupType(subGroupType);
          }
          createWaitForYbcServerTask(new HashSet<>(singletonNodeList))
              .setSubTaskGroupType(subGroupType);
        }
      }

      if (context.postAction != null) {
        context.postAction.accept(node);
      }
      // Run post node upgrade hooks
      createHookTriggerTasks(singletonNodeList, false, true);
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
      UpgradeContext context,
      boolean isYbcPresent) {
    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda,
        mastersAndTServers.getLeft(),
        mastersAndTServers.getRight(),
        context,
        isYbcPresent);
  }

  public void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeContext context,
      boolean isYbcPresent) {

    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda, masterNodes, ServerType.MASTER, context, true, isYbcPresent);

    if (context.processInactiveMaster) {
      createNonRollingUpgradeTaskFlow(
          nonRollingUpgradeLambda,
          getInactiveMasters(masterNodes, tServerNodes),
          ServerType.MASTER,
          context,
          false,
          isYbcPresent);
    }
    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda, tServerNodes, ServerType.TSERVER, context, true, isYbcPresent);
  }

  private List<NodeDetails> getInactiveMasters(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    Universe universe = getUniverse();
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    return tServerNodes != null
        ? tServerNodes.stream()
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
      boolean activeRole,
      boolean isYbcPresent) {
    if ((nodes == null) || nodes.isEmpty()) {
      return;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();

    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);

    if (context.runBeforeStopping) {
      nonRollingUpgradeLambda.run(nodes, Collections.singleton(processType));
    }

    if (isYbcPresent) {
      createServerControlTasks(nodes, ServerType.CONTROLLER, "stop")
          .setSubTaskGroupType(subGroupType);
    }
    createServerControlTasks(nodes, processType, "stop").setSubTaskGroupType(subGroupType);

    if (!context.runBeforeStopping) {
      nonRollingUpgradeLambda.run(nodes, Collections.singleton(processType));
    }

    if (activeRole) {
      createServerControlTasks(nodes, processType, "start").setSubTaskGroupType(subGroupType);
      createWaitForServersTasks(nodes, processType)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      if (isYbcPresent) {
        createServerControlTasks(nodes, ServerType.CONTROLLER, "start")
            .setSubTaskGroupType(subGroupType);
        createWaitForYbcServerTask(new HashSet<NodeDetails>(nodes))
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
      // If there are no universe keys on the universe, it will have no effect.
      if (EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
        createSetActiveUniverseKeysTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
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
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      UpgradeOption upgradeOption,
      boolean isYbcPresent) {
    createRestartTasks(
        mastersAndTServers.getLeft(), mastersAndTServers.getRight(), upgradeOption, isYbcPresent);
  }

  private void createRestartTasks(
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeOption upgradeOption,
      boolean isYbcPresent) {
    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE
        && upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      throw new IllegalArgumentException("Restart can only be either rolling or non-rolling");
    }

    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      createRollingUpgradeTaskFlow(
          (nodes, processType) -> {}, masterNodes, tServerNodes, DEFAULT_CONTEXT, isYbcPresent);
    } else {
      createNonRollingUpgradeTaskFlow(
          (nodes, processType) -> {}, masterNodes, tServerNodes, DEFAULT_CONTEXT, isYbcPresent);
    }
  }

  protected SubTaskGroup createClusterUserIntentUpdateTask(UUID clutserUUID, UUID imageBundleUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateClusterUserIntent");
    UpdateClusterUserIntent.Params updateClusterUserIntentParams =
        new UpdateClusterUserIntent.Params();
    updateClusterUserIntentParams.setUniverseUUID(taskParams().getUniverseUUID());
    updateClusterUserIntentParams.clusterUUID = clutserUUID;
    updateClusterUserIntentParams.imageBundleUUID = imageBundleUUID;

    UpdateClusterUserIntent updateClusterUserIntentTask = createTask(UpdateClusterUserIntent.class);
    updateClusterUserIntentTask.initialize(updateClusterUserIntentParams);
    updateClusterUserIntentTask.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(updateClusterUserIntentTask);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected void createServerConfFileUpdateTasks(
      UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes,
      Map<String, String> masterGflags,
      Map<String, String> tserverGflags) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.UpdatingGFlags, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      ServerType processType = getSingle(processTypes);
      Map<String, String> oldGflags;
      Map<String, String> newGflags;
      if (processType == ServerType.MASTER) {
        newGflags = masterGflags;
        oldGflags = getUserIntent().masterGFlags;
      } else if (processType == ServerType.TSERVER) {
        newGflags = tserverGflags;
        oldGflags = getUserIntent().tserverGFlags;
      } else {
        throw new IllegalStateException("Unknown process type for updating gflags " + processType);
      }
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(userIntent, node, processType, oldGflags, newGflags));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected AnsibleConfigureServers getAnsibleConfigureServerTask(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      NodeDetails node,
      ServerType processType,
      Map<String, String> oldGflags,
      Map<String, String> newGflags) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            userIntent, node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None);
    params.gflags = newGflags;
    params.gflagsToRemove = GFlagsUtil.getDeletedGFlags(oldGflags, newGflags);
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    return task;
  }

  /**
   * Create a task to update server conf files on DB nodes.
   *
   * @param userIntent modified user intent of the current cluster.
   * @param nodes set of nodes on which the file needs to be updated.
   * @param processTypes set of processes whose conf files need to be updated.
   * @param curCluster current cluster.
   * @param curClusters set of current clusters.
   * @param newCluster updated new cluster.
   * @param newClusters set of updated new clusters.
   */
  protected void createServerConfFileUpdateTasks(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      List<NodeDetails> nodes,
      Set<ServerType> processTypes,
      UniverseDefinitionTaskParams.Cluster curCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> curClusters,
      UniverseDefinitionTaskParams.Cluster newCluster,
      Collection<UniverseDefinitionTaskParams.Cluster> newClusters) {
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
      Map<String, String> newGFlags =
          GFlagsUtil.getGFlagsForNode(node, processType, newCluster, newClusters);
      Map<String, String> oldGFlags =
          GFlagsUtil.getGFlagsForNode(node, processType, curCluster, curClusters);
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(userIntent, node, processType, oldGFlags, newGFlags));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected void checkForbiddenToOverrideGFlags(
      NodeDetails node,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Universe universe,
      ServerType processType,
      Map<String, String> newGFlags,
      Config config) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            userIntent, node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None);

    String errorMsg =
        GFlagsUtil.checkForbiddenToOverride(node, params, userIntent, universe, newGFlags, config);
    if (errorMsg != null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          errorMsg
              + ". It is not advised to set these internal gflags. If you want to do it"
              + " forcefully - set runtime config value for "
              + "'yb.gflags.allow_user_override' to 'true'");
    }
  }

  protected ServerType getSingle(Set<ServerType> processTypes) {
    Set<ServerType> filteredServerTypes = new HashSet<>();
    for (ServerType serverType : processTypes) {
      if (!canBeIgnoredServerTypes.contains(serverType)) {
        filteredServerTypes.add(serverType);
      }
    }
    if (filteredServerTypes.size() != 1) {
      throw new IllegalArgumentException("Expected to have single element, got " + processTypes);
    }
    return filteredServerTypes.iterator().next();
  }

  private List<NodeDetails> filterForClusters(List<NodeDetails> nodes) {
    Set<UUID> clusterUUIDs =
        taskParams().clusters.stream().map(c -> c.uuid).collect(Collectors.toSet());
    return nodes.stream()
        .filter(n -> clusterUUIDs.contains(n.placementUuid))
        .collect(Collectors.toList());
  }

  public LinkedHashSet<NodeDetails> fetchNodesForCluster() {
    return toOrderedSet(
        new ImmutablePair<>(
            filterForClusters(fetchMasterNodes(taskParams().upgradeOption)),
            filterForClusters(fetchTServerNodes(taskParams().upgradeOption))));
  }

  public LinkedHashSet<NodeDetails> fetchAllNodes(UpgradeOption upgradeOption) {
    return toOrderedSet(fetchNodes(upgradeOption));
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
    return nodes.stream()
        .sorted(
            Comparator.<NodeDetails, Boolean>comparing(node -> node.state == NodeState.Live)
                .thenComparing(node -> leaderMasterAddress.equals(node.cloudInfo.private_ip))
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
    return nodes.stream()
        .sorted(
            Comparator.<NodeDetails, Boolean>comparing(
                    // Fully upgrade primary cluster first
                    node -> !node.placementUuid.equals(primaryClusterUuid))
                .thenComparing(node -> node.state == NodeState.Live)
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

  // Get the TriggerType for the given situation and trigger the hooks
  private void createHookTriggerTasks(
      Collection<NodeDetails> nodes, boolean isPre, boolean isRolling) {
    String triggerName = (isPre ? "Pre" : "Post") + this.getClass().getSimpleName();
    if (isRolling) triggerName += "NodeUpgrade";
    Optional<TriggerType> optTrigger = TriggerType.maybeResolve(triggerName);
    if (optTrigger.isPresent())
      HookInserter.addHookTrigger(optTrigger.get(), this, taskParams(), nodes);
  }

  @Value
  @Builder
  protected static class UpgradeContext {
    boolean reconfigureMaster;
    boolean runBeforeStopping;
    boolean processInactiveMaster;
    // Set this field to access client userIntent during runtime as
    // usually universeDetails are updated only at the end of task.
    UniverseDefinitionTaskParams.UserIntent userIntent;
    @Builder.Default boolean skipStartingProcesses = false;
    String targetSoftwareVersion;
    Consumer<NodeDetails> postAction;
  }
}
