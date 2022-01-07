// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
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
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public abstract class UpgradeTaskBase extends UniverseDefinitionTaskBase {
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
      UpgradeOption upgradeOption,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      boolean runBeforeStopping) {
    switch (upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(lambda, mastersAndTServers, runBeforeStopping);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(lambda, mastersAndTServers, runBeforeStopping);
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
      boolean runBeforeStopping) {
    switch (upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(lambda, masterNodes, tServerNodes, runBeforeStopping);
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(lambda, masterNodes, tServerNodes, runBeforeStopping);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTaskFlow(lambda, masterNodes, tServerNodes);
        break;
    }
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      Pair<List<NodeDetails>, List<NodeDetails>> mastersAndTServers,
      boolean runBeforeStopping) {
    createRollingUpgradeTaskFlow(
        rollingUpgradeLambda,
        mastersAndTServers.getLeft(),
        mastersAndTServers.getRight(),
        runBeforeStopping);
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      boolean runBeforeStopping) {
    if (masterNodes != null && !masterNodes.isEmpty()) {
      createRollingUpgradeTaskFlow(
          rollingUpgradeLambda, masterNodes, ServerType.MASTER, runBeforeStopping);
    }
    if (tServerNodes != null && !tServerNodes.isEmpty()) {
      createRollingUpgradeTaskFlow(
          rollingUpgradeLambda, tServerNodes, ServerType.TSERVER, runBeforeStopping);
    }
  }

  public void createRollingUpgradeTaskFlow(
      IUpgradeSubTask rollingUpgradeLambda,
      List<NodeDetails> nodes,
      ServerType processType,
      boolean runBeforeStopping) {
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();
    int sleepTime = getSleepTimeForProcess(processType);
    // Need load balancer on to perform leader blacklist
    if (processType == ServerType.TSERVER && !isBlacklistLeaders) {
      createLoadBalancerStateChangeTask(false).setSubTaskGroupType(subGroupType);
      isLoadBalancerOn = false;
    }

    if (processType == ServerType.TSERVER && isBlacklistLeaders) {
      createModifyBlackListTask(nodes, false /* isAdd */, true /* isLeaderBlacklist */)
          .setSubTaskGroupType(subGroupType);
    }

    for (NodeDetails node : nodes) {
      List<NodeDetails> singletonNodeList = Collections.singletonList(node);
      boolean isLeaderBlacklistValidRF = isLeaderBlacklistValidRF(node.nodeName);
      createSetNodeStateTask(node, nodeState).setSubTaskGroupType(subGroupType);
      if (runBeforeStopping) rollingUpgradeLambda.run(singletonNodeList, processType);
      // set leader blacklist and poll
      if (processType == ServerType.TSERVER && isBlacklistLeaders && isLeaderBlacklistValidRF) {
        createModifyBlackListTask(
                Arrays.asList(node), true /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
        createWaitForLeaderBlacklistCompletionTask(leaderBacklistWaitTimeMs)
            .setSubTaskGroupType(subGroupType);
      }
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
      if (!runBeforeStopping) rollingUpgradeLambda.run(singletonNodeList, processType);
      createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
      createWaitForServersTasks(singletonNodeList, processType).setSubTaskGroupType(subGroupType);
      createWaitForServerReady(node, processType, sleepTime).setSubTaskGroupType(subGroupType);
      createWaitForKeyInMemoryTask(node).setSubTaskGroupType(subGroupType);
      // remove leader blacklist
      if (processType == ServerType.TSERVER && isBlacklistLeaders && isLeaderBlacklistValidRF) {
        createModifyBlackListTask(
                Arrays.asList(node), false /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(subGroupType);
      }

      createWaitForFollowerLagTask(node, processType).setSubTaskGroupType(subGroupType);
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
      boolean runBeforeStopping) {
    createNonRollingUpgradeTaskFlow(
        nonRollingUpgradeLambda,
        mastersAndTServers.getLeft(),
        mastersAndTServers.getRight(),
        runBeforeStopping);
  }

  public void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      boolean runBeforeStopping) {
    if (masterNodes != null && !masterNodes.isEmpty()) {
      createNonRollingUpgradeTaskFlow(
          nonRollingUpgradeLambda, masterNodes, ServerType.MASTER, runBeforeStopping);
    }
    if (tServerNodes != null && !tServerNodes.isEmpty()) {
      createNonRollingUpgradeTaskFlow(
          nonRollingUpgradeLambda, tServerNodes, ServerType.TSERVER, runBeforeStopping);
    }
  }

  public void createNonRollingUpgradeTaskFlow(
      IUpgradeSubTask nonRollingUpgradeLambda,
      List<NodeDetails> nodes,
      ServerType processType,
      boolean runBeforeStopping) {
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    NodeState nodeState = getNodeState();

    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);
    if (runBeforeStopping) nonRollingUpgradeLambda.run(nodes, processType);
    createServerControlTasks(nodes, processType, "stop").setSubTaskGroupType(subGroupType);
    if (!runBeforeStopping) nonRollingUpgradeLambda.run(nodes, processType);
    createServerControlTasks(nodes, processType, "start").setSubTaskGroupType(subGroupType);
    createSetNodeStateTasks(nodes, NodeState.Live).setSubTaskGroupType(subGroupType);

    createWaitForServersTasks(nodes, processType)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
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
    if (masterNodes != null && !masterNodes.isEmpty()) {
      createNonRestartUpgradeTaskFlow(nonRestartUpgradeLambda, masterNodes, ServerType.MASTER);
    }
    if (tServerNodes != null && !tServerNodes.isEmpty()) {
      createNonRestartUpgradeTaskFlow(nonRestartUpgradeLambda, tServerNodes, ServerType.TSERVER);
    }
  }

  public void createNonRestartUpgradeTaskFlow(
      IUpgradeSubTask nonRestartUpgradeLambda, List<NodeDetails> nodes, ServerType processType) {
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

  public void createRestartTasks(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes, UpgradeOption upgradeOption) {
    if (upgradeOption != UpgradeOption.ROLLING_UPGRADE
        && upgradeOption != UpgradeOption.NON_ROLLING_UPGRADE) {
      throw new IllegalArgumentException("Restart can only be either rolling or non-rolling");
    }

    if (upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      createRollingUpgradeTaskFlow(
          (List<NodeDetails> nodes, ServerType processType) -> {}, masterNodes, tServerNodes, true);
    } else {
      createNonRollingUpgradeTaskFlow(
          (List<NodeDetails> nodes, ServerType processType) -> {}, masterNodes, tServerNodes, true);
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
    Map<String, String> gflags =
        processType.equals(ServerType.MASTER) ? userIntent.masterGFlags : userIntent.tserverGFlags;
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
    // Sets the isMaster field
    params.isMaster = node.isMaster;
    params.enableYSQL = userIntent.enableYSQL;
    params.enableYCQL = userIntent.enableYCQL;
    params.enableYCQLAuth = userIntent.enableYCQLAuth;
    params.enableYSQLAuth = userIntent.enableYSQLAuth;

    // The software package to install for this cluster.
    params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
    // Set the InstanceType
    params.instanceType = node.cloudInfo.instance_type;
    params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;

    params.allowInsecure = taskParams().allowInsecure;
    params.setTxnTableWaitCountFlag = taskParams().setTxnTableWaitCountFlag;
    params.enableYEDIS = userIntent.enableYEDIS;

    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    UUID custUUID = Customer.get(universe.customerId).uuid;
    params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);
    params.rootCA = universe.getUniverseDetails().rootCA;
    params.clientRootCA = universe.getUniverseDetails().clientRootCA;
    params.rootAndClientRootCASame = universe.getUniverseDetails().rootAndClientRootCASame;

    // Add testing flag.
    params.itestS3PackagePath = taskParams().itestS3PackagePath;
    // Add task type
    params.type = type;
    params.setProperty("processType", processType.toString());
    params.setProperty("taskSubType", taskSubType.toString());
    params.gflags = gflags;
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
}
