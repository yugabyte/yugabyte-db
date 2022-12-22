// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public abstract class KubernetesUpgradeTaskBase extends KubernetesTaskBase {

  protected KubernetesUpgradeTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected UpgradeTaskParams taskParams() {
    return (UpgradeTaskParams) taskParams;
  }

  public abstract SubTaskGroupType getTaskSubGroupType();

  // Wrapper that takes care of common pre and post upgrade tasks and user has
  // flexibility to manipulate subTaskGroupQueue through the lambda passed in parameter
  public void runUpgrade(Runnable upgradeLambda) {
    try {
      isBlacklistLeaders =
          runtimeConfigFactory.forUniverse(getUniverse()).getBoolean(Util.BLACKLIST_LEADERS);
      leaderBacklistWaitTimeMs =
          runtimeConfigFactory
              .forUniverse(getUniverse())
              .getInt(Util.BLACKLIST_LEADER_WAIT_TIME_MS);
      checkUniverseVersion();
      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      if (taskParams().nodePrefix == null) {
        taskParams().nodePrefix = universe.getUniverseDetails().nodePrefix;
      }
      if (taskParams().clusters == null || taskParams().clusters.isEmpty()) {
        taskParams().clusters = universe.getUniverseDetails().clusters;
      }

      // This value is used by subsequent calls to helper methods for
      // creating KubernetesCommandExecutor tasks. This value cannot
      // be changed once set during the Universe creation, so we don't
      // allow users to modify it later during edit, upgrade, etc.
      taskParams().useNewHelmNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;

      // Execute the lambda which populates subTaskGroupQueue
      upgradeLambda.run();

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error={}.", getName(), t);

      // Clear the previous subtasks if any.
      getRunnableTask().reset();
      // If the task failed, we don't want the loadbalancer to be
      // disabled, so we enable it again in case of errors.
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(getTaskSubGroupType());
      getRunnableTask().runSubTasks();

      throw t;
    } finally {
      try {
        if (isBlacklistLeaders) {
          // Clear the previous subtasks if any.
          getRunnableTask().reset();
          List<NodeDetails> tServerNodes = getUniverse().getTServers();
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

  public void createUpgradeTask(
      Universe universe,
      String softwareVersion,
      boolean isMasterChanged,
      boolean isTServerChanged) {
    createUpgradeTask(
        universe, softwareVersion, isMasterChanged, isTServerChanged, CommandType.HELM_UPGRADE);
  }

  public void createUpgradeTask(
      Universe universe,
      String softwareVersion,
      boolean isMasterChanged,
      boolean isTServerChanged,
      CommandType commandType) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    Cluster primaryCluster = universeDetails.getPrimaryCluster();
    PlacementInfo placementInfo = primaryCluster.placementInfo;
    createSingleKubernetesExecutorTask(
        universe.name, CommandType.POD_INFO, placementInfo, /*isReadOnlyCluster*/ false);

    KubernetesPlacement placement =
        new KubernetesPlacement(placementInfo, /*isReadOnlyCluster*/ false);
    Provider provider =
        Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));
    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

    String universeOverrides = primaryCluster.userIntent.universeOverrides;
    Map<String, String> azOverrides = primaryCluster.userIntent.azOverrides;
    if (azOverrides == null) {
      azOverrides = new HashMap<String, String>();
    }

    String masterAddresses =
        KubernetesUtil.computeMasterAddresses(
            placementInfo,
            placement.masters,
            taskParams().nodePrefix,
            universe.name,
            provider,
            universeDetails.communicationPorts.masterRpcPort,
            newNamingStyle);

    if (isMasterChanged) {
      upgradePodsTask(
          universe.name,
          placement,
          masterAddresses,
          null,
          ServerType.MASTER,
          softwareVersion,
          taskParams().sleepAfterMasterRestartMillis,
          universeOverrides,
          azOverrides,
          isMasterChanged,
          isTServerChanged,
          newNamingStyle,
          /*isReadOnlyCluster*/ false,
          commandType);
    }

    if (isTServerChanged) {
      if (!isBlacklistLeaders) {
        createLoadBalancerStateChangeTask(false).setSubTaskGroupType(getTaskSubGroupType());
      }

      upgradePodsTask(
          universe.name,
          placement,
          masterAddresses,
          null,
          ServerType.TSERVER,
          softwareVersion,
          taskParams().sleepAfterTServerRestartMillis,
          universeOverrides,
          azOverrides,
          false, // master change is false since it has already been upgraded.
          isTServerChanged,
          newNamingStyle,
          /*isReadOnlyCluster*/ false,
          commandType);

      // Handle read cluster upgrade.
      if (universeDetails.getReadOnlyClusters().size() != 0) {
        PlacementInfo readClusterPlacementInfo =
            universeDetails.getReadOnlyClusters().get(0).placementInfo;
        createSingleKubernetesExecutorTask(
            universe.name,
            CommandType.POD_INFO,
            readClusterPlacementInfo, /*isReadOnlyCluster*/
            true);

        KubernetesPlacement readClusterPlacement =
            new KubernetesPlacement(readClusterPlacementInfo, /*isReadOnlyCluster*/ true);

        upgradePodsTask(
            universe.name,
            readClusterPlacement,
            masterAddresses,
            null,
            ServerType.TSERVER,
            softwareVersion,
            taskParams().sleepAfterTServerRestartMillis,
            universeOverrides,
            azOverrides,
            false, // master change is false since it has already been upgraded.
            isTServerChanged,
            newNamingStyle,
            /*isReadOnlyCluster*/ true,
            commandType);
      }
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(getTaskSubGroupType());
    }
  }
}
