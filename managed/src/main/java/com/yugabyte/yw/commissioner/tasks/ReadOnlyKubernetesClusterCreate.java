/*
 * Copyright 2022 YugaByte, Inc. and Contributors
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
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Tracks the read only kubernetes cluster create intent within an existing universe.
@Slf4j
@Abortable
@Retryable
public class ReadOnlyKubernetesClusterCreate extends KubernetesTaskBase {
  private YbcManager ybcManager;

  @Inject
  protected ReadOnlyKubernetesClusterCreate(
      BaseTaskDependencies baseTaskDependencies, YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.ybcManager = ybcManager;
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().getUniverseUUID());
    try {
      verifyParams(UniverseOpType.CREATE);
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);
      preTaskActions(universe);
      addBasicPrecheckTasks();

      // Set all the in-memory node names first.
      setNodeNames(universe);

      Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();

      universe = writeUserIntentToUniverse(true);

      Cluster readOnlyCluster = taskParams().getReadOnlyClusters().get(0);
      PlacementInfo pi = readOnlyCluster.placementInfo;

      Provider primaryProvider =
          Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));
      Provider provider =
          Provider.getOrBadRequest(UUID.fromString(readOnlyCluster.userIntent.provider));

      KubernetesPlacement placement = new KubernetesPlacement(pi, /*isReadOnlyCluster*/ true);

      CloudType primaryCloudType = primaryCluster.userIntent.providerType;
      if (primaryCloudType != CloudType.kubernetes) {
        String msg =
            String.format(
                "Expected primary cluster on kubernetes but found on %s", primaryCloudType.name());
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
          getPodsToAdd(placement.tservers, null, ServerType.TSERVER, isMultiAz, true);

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(tserversAdded, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Install YBC on the RR tservers and wait for its completion
      if (universe.isYbcEnabled()) {
        installYbcOnThePods(
            universe.getName(),
            tserversAdded,
            true,
            ybcManager.getStableYbcVersion(),
            readOnlyCluster.userIntent.ybcFlags);
        createWaitForYbcServerTask(tserversAdded);
      }

      // Persist the placement info into the YB master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for a master leader to hear from all the tservers.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      createSwamperTargetUpdateTask(false);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
