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
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class EditKubernetesUniverse extends KubernetesTaskBase {

  static final int DEFAULT_WAIT_TIME_MS = 10000;

  @Inject
  protected EditKubernetesUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    try {
      checkUniverseVersion();
      // Verify the task params.
      verifyParams(UniverseOpType.EDIT);

      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

      // This value is used by subsequent calls to helper methods for
      // creating KubernetesCommandExecutor tasks. This value cannot
      // be changed once set during the Universe creation, so we don't
      // allow users to modify it later during edit, upgrade, etc.
      taskParams().useNewHelmNamingStyle = universeDetails.useNewHelmNamingStyle;

      preTaskActions();
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      if (primaryCluster == null) { // True in case of only readcluster edit.
        primaryCluster = universeDetails.getPrimaryCluster();
      }

      Provider provider = Provider.get(UUID.fromString(primaryCluster.userIntent.provider));

      /* Steps for multi-cluster edit
      1) Compute masters with the new placement info.
      2) Validate params.
      3) For primary cluster if the masters are different from the old one, continue with step 4,
         else go to step 7.
      4) Check if the instance type has changed from xsmall/dev to something else. If so,
         roll all the current masters.
      5) Bring up the new master pods while ensuring nothing else changes in the old deployments.
      6) Create change config task to update the new master addresses
         (adding one and removing one at a time).
      7) Make changes to the primary cluster tservers. Either adding new ones and/or updating the
         current ones.
      8) Create the blacklist to remove unnecessary tservers from the universe.
      9) Wait for the data to move.
      10) For read cluster, update pods if either tserver pods changed or masters are updated.
      11) Create the blacklist to remove unnecessary tservers from the universe.
      12) Wait for the data to move.
      13) Remove the old masters and tservers.
      */

      PlacementInfo primaryPI = primaryCluster.placementInfo;
      int numMasters = primaryCluster.userIntent.replicationFactor;
      PlacementInfoUtil.selectNumMastersAZ(primaryPI, numMasters);
      KubernetesPlacement primaryPlacement =
          new KubernetesPlacement(primaryPI, /*isReadOnlyCluster*/ false);

      boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

      String masterAddresses =
          PlacementInfoUtil.computeMasterAddresses(
              primaryPI,
              primaryPlacement.masters,
              taskParams().nodePrefix,
              provider,
              universeDetails.communicationPorts.masterRpcPort,
              newNamingStyle);

      // validate clusters
      for (Cluster cluster : taskParams().clusters) {
        Cluster currCluster = universeDetails.getClusterByUuid(cluster.uuid);
        validateEditParams(cluster, currCluster);
      }

      // Update the user intent.
      // This writes placement info and user intent of all clusters to DB.
      writeUserIntentToUniverse();

      // primary cluster edit.
      boolean mastersAddrChanged =
          editCluster(
              universe,
              taskParams().getPrimaryCluster(),
              universeDetails.getPrimaryCluster(),
              masterAddresses,
              /*restartAllPods*/ false);

      // read cluster edit.
      for (Cluster cluster : taskParams().clusters) {
        if (cluster.clusterType == ClusterType.ASYNC) {
          editCluster(
              universe,
              cluster,
              universeDetails.getClusterByUuid(cluster.uuid),
              masterAddresses,
              mastersAddrChanged);
        }
      }

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
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

  /*
   * If newCluster is primary cluster, it returns true if there is change in master addresses.
   * Any other case it returns false.
   */
  private boolean editCluster(
      Universe universe,
      Cluster newCluster,
      Cluster curCluster,
      String masterAddresses,
      boolean restartAllPods) {
    if (newCluster == null) {
      return false;
    }

    UserIntent newIntent = newCluster.userIntent, curIntent = curCluster.userIntent;
    PlacementInfo newPI = newCluster.placementInfo, curPI = curCluster.placementInfo;

    boolean isReadOnlyCluster = newCluster.clusterType == ClusterType.ASYNC;
    if (!isReadOnlyCluster) {
      // Can't call this method on read cluster as UI sends non zero rep factor for read replica
      // cluster also.
      // This method uses rep factor to place masters.
      selectNumMastersAZ(newPI);
    }
    KubernetesPlacement newPlacement = new KubernetesPlacement(newPI, isReadOnlyCluster),
        curPlacement = new KubernetesPlacement(curPI, isReadOnlyCluster);
    boolean isMultiAZ =
        PlacementInfoUtil.isMultiAZ(Provider.getOrBadRequest(UUID.fromString(newIntent.provider)));

    boolean instanceTypeChanged = false;
    if (!curIntent.instanceType.equals(newIntent.instanceType)) {
      List<String> masterResourceChangeInstances = Arrays.asList("dev", "xsmall");
      // If the instance type changed from dev/xsmall to anything else,
      // master resources will also change.
      if (!isReadOnlyCluster
          && !curIntent.instanceType.equals(newIntent.instanceType)
          && masterResourceChangeInstances.contains(curIntent.instanceType)) {
        restartAllPods = true;
      }
      instanceTypeChanged = true;
    }

    Set<NodeDetails> mastersToAdd =
        getPodsToAdd(
            newPlacement.masters,
            curPlacement.masters,
            ServerType.MASTER,
            isMultiAZ,
            isReadOnlyCluster);
    Set<NodeDetails> mastersToRemove =
        getPodsToRemove(
            newPlacement.masters,
            curPlacement.masters,
            ServerType.MASTER,
            universe,
            isMultiAZ,
            isReadOnlyCluster);
    Set<NodeDetails> tserversToAdd =
        getPodsToAdd(
            newPlacement.tservers,
            curPlacement.tservers,
            ServerType.TSERVER,
            isMultiAZ,
            isReadOnlyCluster);
    Set<NodeDetails> tserversToRemove =
        getPodsToRemove(
            newPlacement.tservers,
            curPlacement.tservers,
            ServerType.TSERVER,
            universe,
            isMultiAZ,
            isReadOnlyCluster);

    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;
    PlacementInfo activeZones = new PlacementInfo();
    for (UUID currAZs : curPlacement.configs.keySet()) {
      PlacementInfoUtil.addPlacementZone(currAZs, activeZones);
    }

    // Bring up new masters and update the configs.
    // No need to check mastersToRemove as total number of masters is invariant.
    if (!mastersToAdd.isEmpty()) {
      // If starting new masters, we want them to come up in shell-mode.
      restartAllPods = true;
      startNewPods(
          mastersToAdd,
          ServerType.MASTER,
          activeZones,
          isReadOnlyCluster,
          /*masterAddresses*/ "",
          newPlacement,
          curPlacement);

      // Update master addresses to the latest required ones.
      createMoveMasterTasks(new ArrayList<>(mastersToAdd), new ArrayList<>(mastersToRemove));
    }

    // Bring up new tservers.
    if (!tserversToAdd.isEmpty()) {
      startNewPods(
          tserversToAdd,
          ServerType.TSERVER,
          activeZones,
          isReadOnlyCluster,
          masterAddresses,
          newPlacement,
          curPlacement);
    }

    // Update the blacklist servers on master leader.
    createPlacementInfoTask(tserversToRemove)
        .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

    // If the tservers have been removed, move the data.
    if (!tserversToRemove.isEmpty()) {
      createWaitForDataMoveTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    }
    // If tservers have been added, we wait for the load to balance.
    if (!tserversToAdd.isEmpty()) {
      createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    }

    // Now roll all the old pods that haven't been removed and aren't newly added.
    // This will update the master addresses as well as the instance type changes.
    if (restartAllPods) {
      updateRemainingPods(
          ServerType.MASTER,
          newPlacement,
          curPlacement,
          masterAddresses,
          true,
          true,
          newNamingStyle,
          isReadOnlyCluster);
    }
    if (instanceTypeChanged || restartAllPods) {
      updateRemainingPods(
          ServerType.TSERVER,
          newPlacement,
          curPlacement,
          masterAddresses,
          false,
          true,
          newNamingStyle,
          isReadOnlyCluster);
    }

    // If tservers have been removed, check if some deployments need to be completely
    // removed or scaled down. Also modify the blacklist to untrack deleted pods.
    if (!tserversToRemove.isEmpty()) {
      // Need to unify with DestroyKubernetesUniverse.
      // Using currPlacement, newPlacement we figure out what pods need to be removed. So no need to
      // pass tserversRemoved.
      deletePodsTask(
          curPlacement,
          masterAddresses,
          newPlacement,
          instanceTypeChanged,
          isMultiAZ,
          isReadOnlyCluster);
      createModifyBlackListTask(
              new ArrayList<>(tserversToRemove), false /* isAdd */, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Update the universe to the new state.
    createSingleKubernetesExecutorTask(
        KubernetesCommandExecutor.CommandType.POD_INFO, newPI, isReadOnlyCluster);
    return !mastersToAdd.isEmpty();
  }

  private void validateEditParams(Cluster newCluster, Cluster curCluster) {
    // TODO we should look for y(c)sql auth, gflags changes and so on.
    if (newCluster.userIntent.replicationFactor != curCluster.userIntent.replicationFactor) {
      String msg =
          String.format(
              "Replication factor can't be changed during the edit operation. "
                  + "Previous rep factor: %d, current rep factor %d for cluster type: %s",
              newCluster.userIntent.replicationFactor,
              curCluster.userIntent.replicationFactor,
              newCluster.clusterType);
      log.error(msg);
      throw new IllegalArgumentException(msg);
    }

    String newProviderStr = newCluster.userIntent.provider;
    String currProviderStr = curCluster.userIntent.provider;

    if (!newProviderStr.equals(currProviderStr)) {
      String msg =
          String.format(
              "Provider can't change during editing of the universe. "
                  + "Expected provider %s but found %s for cluster type: %s",
              currProviderStr, newProviderStr, newCluster.clusterType);
      log.error(msg);
      throw new IllegalArgumentException(msg);
    }
  }

  /*
  Sends the RPC to update the master addresses in the config.
  */
  public void createMoveMasterTasks(
      List<NodeDetails> mastersToAdd, List<NodeDetails> mastersToRemove) {

    UserTaskDetails.SubTaskGroupType subTask = SubTaskGroupType.WaitForDataMigration;

    // Perform adds.
    for (int idx = 0; idx < mastersToAdd.size(); idx++) {
      createChangeConfigTask(mastersToAdd.get(idx), true, subTask);
    }
    // Perform removes.
    for (int idx = 0; idx < mastersToRemove.size(); idx++) {
      createChangeConfigTask(mastersToRemove.get(idx), false, subTask);
    }
    // Wait for master leader.
    createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  /*
  Starts up the new pods as requested by the user.
  */
  public void startNewPods(
      Set<NodeDetails> podsToAdd,
      ServerType serverType,
      PlacementInfo activeZones,
      boolean isReadOnlyCluster,
      String masterAddresses,
      KubernetesPlacement newPlacement,
      KubernetesPlacement currPlacement) {
    createPodsTask(
        newPlacement, masterAddresses, currPlacement, serverType, activeZones, isReadOnlyCluster);

    createSingleKubernetesExecutorTask(
        KubernetesCommandExecutor.CommandType.POD_INFO, activeZones, isReadOnlyCluster);

    createWaitForServersTasks(podsToAdd, serverType)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  /*
  Performs the updates to the helm charts to modify the master addresses as well as
  update the instance type.
  */
  public void updateRemainingPods(
      ServerType serverType,
      KubernetesPlacement newPlacement,
      KubernetesPlacement currPlacement,
      String masterAddresses,
      boolean masterChanged,
      boolean tserverChanged,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {

    String ybSoftwareVersion = taskParams().getPrimaryCluster().userIntent.ybSoftwareVersion;

    upgradePodsTask(
        newPlacement,
        masterAddresses,
        currPlacement,
        serverType,
        ybSoftwareVersion,
        DEFAULT_WAIT_TIME_MS,
        masterChanged,
        tserverChanged,
        newNamingStyle,
        isReadOnlyCluster);
  }
}
