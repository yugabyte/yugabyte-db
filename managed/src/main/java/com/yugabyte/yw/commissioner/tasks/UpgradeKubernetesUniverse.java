/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.KubernetesUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpgradeKubernetesUniverse extends KubernetesTaskBase {
  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  protected UpgradeKubernetesUniverse(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
  }

  public static class Params extends KubernetesUpgradeParams {}

  @Override
  protected KubernetesUpgradeParams taskParams() {
    return (KubernetesUpgradeParams) taskParams;
  }

  @Override
  public void run() {
    Throwable th = null;
    try {
      checkUniverseVersion();

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);
      kubernetesStatus.startYBUniverseEventStatus(
          universe,
          taskParams().getKubernetesResourceDetails(),
          TaskType.UpgradeKubernetesUniverse.name(),
          getUserTaskUUID(),
          UniverseState.EDITING);

      taskParams().rootCA = universe.getUniverseDetails().rootCA;

      // This value is used by subsequent calls to helper methods for
      // creating KubernetesCommandExecutor tasks. This value cannot
      // be changed once set during the Universe creation, so we don't
      // allow users to modify it later during edit, upgrade, etc.
      taskParams().useNewHelmNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;

      UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
      PlacementInfo primaryPI = universe.getUniverseDetails().getPrimaryCluster().placementInfo;

      if (taskParams().taskType == UpgradeTaskParams.UpgradeTaskType.Software) {
        if (taskParams().ybSoftwareVersion == null || taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException(
              "Invalid yugabyte software version: " + taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(userIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException(
              "Cluster is already on yugabyte software version: " + taskParams().ybSoftwareVersion);
        }
      }

      preTaskActions();

      KubernetesPlacement primaryPlacement = new KubernetesPlacement(primaryPI, false);
      Provider provider =
          Provider.getOrBadRequest(
              UUID.fromString(taskParams().getPrimaryCluster().userIntent.provider));
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      boolean newNamingStyle = taskParams().useNewHelmNamingStyle;
      String masterAddresses =
          KubernetesUtil.computeMasterAddresses(
              primaryPI,
              primaryPlacement.masters,
              taskParams().nodePrefix,
              universe.getName(),
              provider,
              universeDetails.communicationPorts.masterRpcPort,
              newNamingStyle);

      for (UniverseDefinitionTaskParams.Cluster cluster : taskParams().clusters) {
        PlacementInfo pi = cluster.placementInfo;
        switch (taskParams().taskType) {
          case Software:
            log.info(
                "Upgrading software version to {} in universe {}",
                taskParams().ybSoftwareVersion,
                universe.getName());

            createUpgradeTask(
                cluster.userIntent,
                universe,
                pi,
                cluster.clusterType == ClusterType.ASYNC,
                masterAddresses,
                taskParams().isEnableYbc(),
                taskParams().getYbcSoftwareVersion());

            if (taskParams().upgradeSystemCatalog) {
              createRunYsqlUpgradeTask(taskParams().ybSoftwareVersion)
                  .setSubTaskGroupType(getTaskSubGroupType());
            }

            createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
                .setSubTaskGroupType(getTaskSubGroupType());
            break;
          case GFlags:
            log.info("Upgrading GFlags in universe {}", universe.getName());
            updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
                .setSubTaskGroupType(getTaskSubGroupType());

            createUpgradeTask(
                userIntent,
                universe,
                pi,
                cluster.clusterType == ClusterType.ASYNC,
                masterAddresses,
                taskParams().isEnableYbc(),
                taskParams().getYbcSoftwareVersion());
            break;
        }
      }

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error={}.", getName(), t);

      // If the task failed, we don't want the loadbalancer to be disabled,
      // so we enable it again in case of errors.
      setTaskQueueAndRun(
          () ->
              createLoadBalancerStateChangeTask(true /*enable*/)
                  .setSubTaskGroupType(getTaskSubGroupType()));
      th = t;
      throw t;
    } finally {
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          TaskType.UpgradeKubernetesUniverse.name(),
          getUserTaskUUID(),
          (th != null) ? UniverseState.ERROR_UPDATING : UniverseState.READY,
          th);
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }

  private SubTaskGroupType getTaskSubGroupType() {
    switch (taskParams().taskType) {
      case Software:
        return SubTaskGroupType.UpgradingSoftware;
      case GFlags:
        return SubTaskGroupType.UpdatingGFlags;
      default:
        return SubTaskGroupType.Invalid;
    }
  }

  private void createUpgradeTask(
      UserIntent userIntent,
      Universe universe,
      PlacementInfo pi,
      boolean isReadOnlyCluster,
      String masterAddresses,
      boolean enableYbc,
      String ybcSoftwareVersion) {
    String ybSoftwareVersion = null;
    boolean masterChanged = false;
    boolean tserverChanged = false;
    if (taskParams().taskType == UpgradeTaskParams.UpgradeTaskType.Software) {
      ybSoftwareVersion = taskParams().ybSoftwareVersion;
      if (!isReadOnlyCluster) {
        masterChanged = true;
      }
      tserverChanged = true;
    } else {
      ybSoftwareVersion = userIntent.ybSoftwareVersion;
      if (!taskParams().masterGFlags.equals(userIntent.masterGFlags)) {
        if (!isReadOnlyCluster) {
          masterChanged = true;
        }
      }
      if (!taskParams().tserverGFlags.equals(userIntent.tserverGFlags)) {
        tserverChanged = true;
      }
    }

    createSingleKubernetesExecutorTask(
        universe.getName(), CommandType.POD_INFO, pi, isReadOnlyCluster);

    KubernetesPlacement placement = new KubernetesPlacement(pi, isReadOnlyCluster);

    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

    String universeOverrides =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.universeOverrides;
    Map<String, String> azOverrides =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.azOverrides;
    if (azOverrides == null) {
      azOverrides = new HashMap<String, String>();
    }

    if (masterChanged) {
      userIntent.masterGFlags = taskParams().masterGFlags;
      upgradePodsTask(
          universe.getName(),
          placement,
          masterAddresses,
          null,
          ServerType.MASTER,
          ybSoftwareVersion,
          getOrCreateExecutionContext().getWaitForServerTimeout().toMillis(),
          universeOverrides,
          azOverrides,
          masterChanged,
          tserverChanged,
          newNamingStyle,
          /*isReadOnlyCluster*/ false,
          CommandType.HELM_UPGRADE,
          enableYbc,
          ybcSoftwareVersion,
          /* addDelayAfterStartup */ true);
    }
    if (tserverChanged) {
      createLoadBalancerStateChangeTask(false /*enable*/)
          .setSubTaskGroupType(getTaskSubGroupType());

      userIntent.tserverGFlags = taskParams().tserverGFlags;
      upgradePodsTask(
          universe.getName(),
          placement,
          masterAddresses,
          null,
          ServerType.TSERVER,
          ybSoftwareVersion,
          taskParams().sleepAfterTServerRestartMillis,
          universeOverrides,
          azOverrides,
          false /* master change is false since it has already been upgraded.*/,
          tserverChanged,
          newNamingStyle,
          isReadOnlyCluster,
          CommandType.HELM_UPGRADE,
          enableYbc,
          ybcSoftwareVersion,
          /* addDelayAfterStartup */ true);

      if (enableYbc) {
        if (isReadOnlyCluster) {
          Set<NodeDetails> replicaTservers =
              new HashSet<NodeDetails>(
                  universe.getNodesInCluster(taskParams().getReadOnlyClusters().get(0).uuid));
          installYbcOnThePods(
              universe.getName(),
              replicaTservers,
              true,
              ybcSoftwareVersion,
              taskParams().getReadOnlyClusters().get(0).userIntent.ybcFlags);
          performYbcAction(replicaTservers, true, "stop");
          createWaitForYbcServerTask(replicaTservers);
        } else {
          Set<NodeDetails> primaryTservers =
              new HashSet<NodeDetails>(
                  universe.getNodesInCluster(taskParams().getPrimaryCluster().uuid));
          installYbcOnThePods(
              universe.getName(),
              primaryTservers,
              false,
              ybcSoftwareVersion,
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybcFlags);
          performYbcAction(primaryTservers, true, "stop");
          createWaitForYbcServerTask(primaryTservers);
        }
      }

      createLoadBalancerStateChangeTask(true /*enable*/).setSubTaskGroupType(getTaskSubGroupType());
    }
  }
}
