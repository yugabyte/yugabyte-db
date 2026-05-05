/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Tracks the read only kubernetes cluster create intent within an existing universe.
@Slf4j
@Abortable
@Retryable
public class ReadOnlyKubernetesClusterCreate extends KubernetesTaskBase {
  private YbcManager ybcManager;
  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  protected ReadOnlyKubernetesClusterCreate(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
    this.ybcManager = ybcManager;
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().getUniverseUUID());
    Throwable th = null;
    try {
      verifyParams(UniverseOpType.CREATE);
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, u -> setCommunicationPortsForNodes(false));
      kubernetesStatus.startYBUniverseEventStatus(
          universe,
          taskParams().getKubernetesResourceDetails(),
          TaskType.ReadOnlyKubernetesClusterCreate.name(),
          getUserTaskUUID(),
          UniverseState.EDITING);
      preTaskActions(universe);
      addBasicPrecheckTasks();

      // Set all the in-memory node names first.
      setNodeNames(universe);

      Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();

      universe = writeUserIntentToUniverse(true);

      Cluster readOnlyCluster = taskParams().getReadOnlyClusters().get(0);
      PlacementInfo pi = readOnlyCluster.placementInfo;

      Provider primaryProvider = Util.getSingleProvider(primaryCluster);
      Provider provider = Util.getSingleProvider(readOnlyCluster);

      KubernetesPlacement placement = new KubernetesPlacement(pi, /*isReadOnlyCluster*/ true);

      if (primaryProvider.getCloudCode() != CloudType.kubernetes) {
        String msg =
            String.format(
                "Expected primary cluster on kubernetes but found on %s",
                primaryProvider.getCloudCode());
        log.error(msg);
        throw new IllegalArgumentException(msg);
      }

      PlacementInfo primaryPI = primaryCluster.placementInfo;
      KubernetesPlacement primaryPlacement =
          new KubernetesPlacement(primaryPI, /*isReadOnlyCluster */ false);

      // This value is used by subsequent calls to helper methods for
      // creating KubernetesCommandExecutor tasks. This value cannot
      // be changed once set during the Universe creation, so we don't
      // allow users to modify it later during edit, upgrade, etc.
      taskParams().useNewHelmNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;

      String masterAddresses =
          KubernetesUtil.computeMasterAddresses(
              primaryPI,
              primaryPlacement.masters,
              taskParams().nodePrefix,
              universe.getName(),
              primaryProvider,
              taskParams().communicationPorts.masterRpcPort,
              taskParams().useNewHelmNamingStyle);

      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
      createPodsTask(universe.getName(), placement, masterAddresses, true, universe.isYbcEnabled());

      // Following method assumes primary cluster.
      createSingleKubernetesExecutorTask(
          universe.getName(), KubernetesCommandExecutor.CommandType.POD_INFO, pi, true);

      Set<NodeDetails> tserversAdded =
          getPodsToAdd(placement, null, ServerType.TSERVER, isMultiAz, true).values().stream()
              .flatMap(nDSet -> nDSet.stream())
              .collect(Collectors.toSet());

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(tserversAdded, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Install YBC on the RR tservers and wait for its completion
      if (universe.isYbcEnabled()) {
        if (!universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()) {
          installYbcOnThePods(
              tserversAdded,
              true,
              ybcManager.getStableYbcVersion(),
              readOnlyCluster.userIntent.ybcFlags);
        } else {
          log.debug("Skipping configure YBC as 'useYBDBInbuiltYbc' is enabled");
        }
        createWaitForYbcServerTask(tserversAdded);
      }

      // Persist the placement info into the YB master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for a master leader to hear from all the tservers.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update PDB policy for the universe.
      if (universe.getUniverseDetails().useNewHelmNamingStyle) {
        createPodDisruptionBudgetPolicyTask(false /* deletePDB */, true /* reCreatePDB */);
      }

      createSwamperTargetUpdateTask(false);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      th = t;
      throw t;
    } finally {
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          TaskType.ReadOnlyKubernetesClusterCreate.name(),
          getUserTaskUUID(),
          (th != null) ? UniverseState.ERROR_UPDATING : UniverseState.READY,
          th);
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
