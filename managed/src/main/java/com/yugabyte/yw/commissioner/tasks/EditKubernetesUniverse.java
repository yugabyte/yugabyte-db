/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallThirdPartySoftwareK8s;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckVolumeExpansion;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesPostExpansionCheckVolume;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
@Abortable
@Retryable
public class EditKubernetesUniverse extends KubernetesTaskBase {

  static final int DEFAULT_WAIT_TIME_MS = 10000;
  private final OperatorStatusUpdater kubernetesStatus;
  private final YbcManager ybcManager;

  @Inject
  protected EditKubernetesUniverse(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      YbcManager ybcManager) {
    super(baseTaskDependencies, kubernetesManagerFactory);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
    this.ybcManager = ybcManager;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      // Verify the task params.
      verifyParams(UniverseOpType.EDIT);
    }
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    addBasicPrecheckTasks();
    if (isFirstTry()) {
      createValidateDiskSizeOnEdit(universe);
    }
    if (universe.getUniverseDetails().getPrimaryCluster().isGeoPartitioned()
        && universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL) {
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      createTablespaceValidationOnRemoveTask(
          primaryCluster.uuid,
          primaryCluster.getOverallPlacement(),
          taskParams().getPrimaryCluster().getPartitions());
    }
  }

  @Override
  public void run() {
    Throwable th = null;
    if (maybeRunOnlyPrechecks()) {
      return;
    }
    try {
      checkUniverseVersion();
      // TODO: Would it make sense to have a precheck k8s task that does
      // some precheck operations to verify kubeconfig, svcaccount, connectivity to universe here ?
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> setCommunicationPortsForNodes(false) /* Txn callback */);

      kubernetesStatus.startYBUniverseEventStatus(
          universe,
          taskParams().getKubernetesResourceDetails(),
          TaskType.EditKubernetesUniverse.name(),
          getUserTaskUUID(),
          UniverseState.EDITING);
      // Reset any state from previous tasks if this is a new invocation.
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      // This value is used by subsequent calls to helper methods for
      // creating KubernetesCommandExecutor tasks. This value cannot
      // be changed once set during the Universe creation, so we don't
      // allow users to modify it later during edit, upgrade, etc.
      taskParams().useNewHelmNamingStyle = universeDetails.useNewHelmNamingStyle;
      // Only cancelling health checks idempotent.
      preTaskActions();
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      if (primaryCluster == null) { // True in case of only readcluster edit.
        primaryCluster = universeDetails.getPrimaryCluster();
      }

      Provider provider =
          Provider.getOrBadRequest(UUID.fromString(primaryCluster.userIntent.provider));

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

      PlacementInfo primaryPI = primaryCluster.getOverallPlacement();
      int numMasters = primaryCluster.userIntent.replicationFactor;
      PlacementInfoUtil.selectNumMastersAZ(
          primaryPI, numMasters, PlacementInfoUtil.getZonesForMasters(primaryCluster));
      KubernetesPlacement primaryPlacement =
          new KubernetesPlacement(primaryPI, /*isReadOnlyCluster*/ false);

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

      Cluster existingPrimaryCluster = universeDetails.getPrimaryCluster();
      PlacementInfo existingPrimaryPI = existingPrimaryCluster.placementInfo;
      int existingNumMasters = existingPrimaryCluster.userIntent.replicationFactor;
      PlacementInfoUtil.selectNumMastersAZ(
          existingPrimaryPI,
          existingNumMasters,
          PlacementInfoUtil.getZonesForMasters(existingPrimaryCluster));
      KubernetesPlacement existingPrimaryPlacement =
          new KubernetesPlacement(existingPrimaryPI, /*isReadOnlyCluster*/ false);
      String existingMasterAddresses =
          KubernetesUtil.computeMasterAddresses(
              existingPrimaryPI,
              existingPrimaryPlacement.masters,
              taskParams().nodePrefix,
              universe.getName(),
              provider,
              universeDetails.communicationPorts.masterRpcPort,
              newNamingStyle);

      // validate clusters
      for (Cluster cluster : taskParams().clusters) {
        Cluster currCluster = universeDetails.getClusterByUuid(cluster.uuid);
        validateEditParams(cluster, currCluster);
      }
      boolean primaryRFChange =
          universeDetails.getPrimaryCluster().userIntent.replicationFactor
              != primaryCluster.userIntent.replicationFactor;

      // Delete old PDB policy for the universe.
      if (universe.getUniverseDetails().useNewHelmNamingStyle) {
        createPodDisruptionBudgetPolicyTask(true /* deletePDB */);
      }

      // Update the user intent.
      // This writes new state of nodes to DB.
      updateUniverseNodesAndSettings(universe, taskParams(), false);
      // primary cluster edit.
      boolean mastersAddrChanged =
          editCluster(
              universe,
              taskParams().getPrimaryCluster(),
              universeDetails.getPrimaryCluster(),
              masterAddresses,
              existingMasterAddresses);
      // Updating cluster in DB
      createUpdateUniverseIntentTask(taskParams().getPrimaryCluster());

      // before staring the read cluster edit, copy over primary cluster into taskParams
      // since the client does not have to pass the primary cluster in the request payload.
      // The primary cluster in taskParams is used by KubernetesCommandExecutor to generate
      // helm overrides.
      if (taskParams().getPrimaryCluster() == null) {
        taskParams()
            .upsertCluster(
                primaryCluster.userIntent,
                primaryCluster.getPartitions(),
                primaryCluster.placementInfo,
                primaryCluster.uuid,
                primaryCluster.clusterType);
      }
      // read cluster edit.
      for (Cluster cluster : taskParams().clusters) {
        if (cluster.clusterType == ClusterType.ASYNC) {
          // Use new master addresses for editing read cluster.
          editCluster(
              universe,
              cluster,
              universeDetails.getClusterByUuid(cluster.uuid),
              masterAddresses,
              masterAddresses /* existingMasterAddresses */,
              mastersAddrChanged);
          // Updating cluster in DB
          createUpdateUniverseIntentTask(cluster);
        }
      }

      if (primaryRFChange) {
        createMasterLeaderStepdownTask();
      }

      // Create new PDB policy for the universe.
      if (universe.getUniverseDetails().useNewHelmNamingStyle) {
        createPodDisruptionBudgetPolicyTask(false /* deletePDB */);
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
      th = t;
      throw t;
    } finally {
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          TaskType.EditKubernetesUniverse.name(),
          getUserTaskUUID(),
          (th != null) ? UniverseState.ERROR_UPDATING : UniverseState.READY,
          th);
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }

  private boolean editCluster(
      Universe universe,
      Cluster newCluster,
      Cluster curCluster,
      String masterAddresses,
      String existingMasterAddresses) {
    return editCluster(
        universe,
        newCluster,
        curCluster,
        masterAddresses,
        existingMasterAddresses,
        false /* masterAddressesChanged */);
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
      String existingMasterAddresses,
      boolean masterAddressesChanged) {
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
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }

    boolean supportsNonRestartGflagsUpgrade =
        KubernetesUtil.isNonRestartGflagsUpgradeSupported(
            primaryCluster.userIntent.ybSoftwareVersion);

    KubernetesPlacement newPlacement = new KubernetesPlacement(newPI, isReadOnlyCluster),
        curPlacement = new KubernetesPlacement(curPI, isReadOnlyCluster);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(newIntent.provider));
    boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

    // Update disk size if there is a change
    boolean tserverDiskSizeChanged =
        !curIntent.deviceInfo.volumeSize.equals(newIntent.deviceInfo.volumeSize);
    boolean masterDiskSizeChanged =
        !(curIntent.masterDeviceInfo == null)
            && !curIntent.masterDeviceInfo.volumeSize.equals(newIntent.masterDeviceInfo.volumeSize);
    if (tserverDiskSizeChanged || masterDiskSizeChanged) {
      if (tserverDiskSizeChanged) {
        log.info(
            "Creating task for tserver disk size change from {} to {}",
            curIntent.deviceInfo.volumeSize,
            newIntent.deviceInfo.volumeSize);
      }
      if (masterDiskSizeChanged) {
        log.info(
            "Creating task for master disk size change from {} to {}",
            curIntent.masterDeviceInfo.volumeSize,
            newIntent.masterDeviceInfo.volumeSize);
      }
      createResizeDiskTask(
          universe.getName(),
          curPlacement,
          newCluster.uuid,
          existingMasterAddresses,
          newIntent,
          isReadOnlyCluster,
          newNamingStyle,
          universe.isYbcEnabled(),
          ybcManager.getStableYbcVersion(),
          tserverDiskSizeChanged,
          masterDiskSizeChanged,
          supportsNonRestartGflagsUpgrade /* usePreviousGflagsChecksum */);
    }

    boolean restartAllPods = false;
    boolean instanceTypeChanged = false;
    // TODO Support overriden instance types
    if (!confGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
      if (!curIntent.instanceType.equals(newIntent.instanceType)) {
        List<String> masterResourceChangeInstances = Arrays.asList("dev", "xsmall");
        // If the instance type changed from dev/xsmall to anything else,
        // master resources will also change.
        if (!isReadOnlyCluster && masterResourceChangeInstances.contains(curIntent.instanceType)) {
          restartAllPods = true;
        }
        instanceTypeChanged = true;
      }
    } else {
      boolean tserverCpuChanged =
          !curIntent.tserverK8SNodeResourceSpec.cpuCoreCount.equals(
              newIntent.tserverK8SNodeResourceSpec.cpuCoreCount);
      boolean tserverMemChanged =
          !curIntent.tserverK8SNodeResourceSpec.memoryGib.equals(
              newIntent.tserverK8SNodeResourceSpec.memoryGib);
      boolean masterMemChanged = false;
      boolean masterCpuChanged = false;

      // For clusters that have read replicas, this condition is true since we
      // do not pass in masterK8sNodeResourceSpec.
      if (curIntent.masterK8SNodeResourceSpec != null
          && newIntent.masterK8SNodeResourceSpec != null) {
        masterMemChanged =
            !curIntent.masterK8SNodeResourceSpec.memoryGib.equals(
                newIntent.masterK8SNodeResourceSpec.memoryGib);
        masterCpuChanged =
            !curIntent.masterK8SNodeResourceSpec.cpuCoreCount.equals(
                newIntent.masterK8SNodeResourceSpec.cpuCoreCount);
      }
      instanceTypeChanged =
          tserverCpuChanged || masterCpuChanged || tserverMemChanged || masterMemChanged;
      if (!isReadOnlyCluster && (masterMemChanged || masterCpuChanged)) {
        restartAllPods = true;
      }
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

    PlacementInfo activeZones = new PlacementInfo();
    for (UUID currAZs : curPlacement.configs.keySet()) {
      PlacementInfoUtil.addPlacementZone(currAZs, activeZones);
    }

    if (!mastersToAdd.isEmpty()) {
      // Bring up new masters and update the configs.
      // No need to check mastersToRemove as total number of masters is invariant.
      // Handle previously executed 'Add' operations on master nodes. To avoid
      // re-initializing these masters, a separate local structure is used for
      // storing a filtered list of uninitialized masters. Note: 'mastersToAdd'
      // is not modified directly to maintain control flow for downstream code.

      Set<NodeDetails> newMasters = new HashSet<>(mastersToAdd); // Make a copy
      // Filter the copy only on a retry.
      if (!isFirstTry()) {
        Set<NodeDetails> toRemove = new HashSet<>();
        for (NodeDetails node : newMasters) {
          if (node.cloudInfo == null) {
            // We didn't even bring this node up yet,
            // so no need to check ChangeMasterConfigDone.
            continue;
          }
          String ipToUse = node.cloudInfo.private_ip;
          boolean alreadyAdded = isChangeMasterConfigDone(universe, node, true, ipToUse);
          if (alreadyAdded) {
            toRemove.add(node);
          }
        }
        newMasters.removeAll(toRemove);
      }
      masterAddressesChanged = true;
      // Start new masters in shell mode with empty masterAddresses.
      startNewPods(
          universe.getName(),
          newMasters,
          ServerType.MASTER,
          activeZones,
          isReadOnlyCluster,
          /*masterAddresses*/ "",
          newPlacement,
          curPlacement,
          false /* enableYbc */,
          /* Use previous gflags checksum only if can use and no pod restart is required */
          supportsNonRestartGflagsUpgrade && !restartAllPods /* usePreviousGflagsChecksum */);

      // Update master addresses to the latest required ones,
      // We use the original unfiltered mastersToAdd which is determined from pi.
      createMoveMasterTasks(new ArrayList<>(mastersToAdd), new ArrayList<>(mastersToRemove));
    }

    // If master address changed and cannot use non-restart flow,
    // should restart pods.
    if (masterAddressesChanged && !supportsNonRestartGflagsUpgrade) {
      restartAllPods = true;
    }

    // Bring up new tservers.
    if (!tserversToAdd.isEmpty()) {
      startNewPods(
          universe.getName(),
          tserversToAdd,
          ServerType.TSERVER,
          activeZones,
          isReadOnlyCluster,
          masterAddresses,
          newPlacement,
          curPlacement,
          universe.isYbcEnabled(),
          /* Use previous gflags checksum only if can use and no pod restart is required */
          supportsNonRestartGflagsUpgrade
              && !(restartAllPods || instanceTypeChanged) /* usePreviousGflagsChecksum */);

      if (universe.isYbcEnabled()) {
        if (!universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()) {
          installYbcOnThePods(
              tserversToAdd,
              isReadOnlyCluster,
              ybcManager.getStableYbcVersion(),
              isReadOnlyCluster
                  ? universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.ybcFlags
                  : universe.getUniverseDetails().getPrimaryCluster().userIntent.ybcFlags,
              newPlacement);
        } else {
          log.debug("Skipping configure YBC as 'useYBDBInbuiltYbc' is enabled");
        }
        createWaitForYbcServerTask(tserversToAdd);
      }
    }

    // Update the blacklist servers on master leader.
    createPlacementInfoTask(tserversToRemove, taskParams().clusters)
        .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

    // If the tservers have been removed, move the data.
    if (!tserversToRemove.isEmpty()) {
      createWaitForDataMoveTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    }

    if (!tserversToAdd.isEmpty()
        && confGetter.getConfForScope(universe, UniverseConfKeys.waitForLbForAddedNodes)) {
      // If tservers have been added, we wait for the load to balance.
      createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
    }

    if (newCluster.isGeoPartitioned() && newCluster.userIntent.enableYSQL) {
      // Currently we rely on user to modify partitions correctly after doing edit.
      // So we don't check tablespace placement, only the existence.
      createTablespacesTasks(newCluster.getPartitions(), true);
    }

    // Handle Namespaced services ownership change/delete
    addHandleKubernetesNamespacedServices(
            false /* readReplicaDelete */,
            taskParams(),
            taskParams().getUniverseUUID(),
            true /* handleOwnershipChanges */)
        .setSubTaskGroupType(SubTaskGroupType.KubernetesHandleNamespacedService);

    String universeOverrides = primaryCluster.userIntent.universeOverrides;
    Map<String, String> azOverrides = primaryCluster.userIntent.azOverrides;
    if (azOverrides == null) {
      azOverrides = new HashMap<String, String>();
    }

    // Add master/tserver pods to list of pods to check safe to take down.
    // Added here since the pods can be removed in any of the subtasks below.
    createCheckNodeSafeToDeleteTasks(universe.getUniverseUUID(), mastersToRemove, tserversToRemove);

    // Now roll all the old pods that haven't been removed and aren't newly added.
    // This will update the master addresses as well as the instance type changes.
    if (restartAllPods) {
      upgradePodsTask(
          universe.getName(),
          newPlacement,
          masterAddresses,
          curPlacement,
          ServerType.MASTER,
          newIntent.ybSoftwareVersion,
          universeOverrides,
          azOverrides,
          newNamingStyle,
          isReadOnlyCluster,
          KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
          universe.isYbcEnabled(),
          ybcManager.getStableYbcVersion(),
          PodUpgradeParams.DEFAULT,
          null /* ysqlMajorVersionUpgradeState */,
          null /* rootCAUUID */);

      upgradePodsTask(
          universe.getName(),
          newPlacement,
          masterAddresses,
          curPlacement,
          ServerType.TSERVER,
          newIntent.ybSoftwareVersion,
          universeOverrides,
          azOverrides,
          newNamingStyle,
          isReadOnlyCluster,
          KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
          universe.isYbcEnabled(),
          ybcManager.getStableYbcVersion(),
          PodUpgradeParams.DEFAULT,
          null /* ysqlMajorVersionUpgradeState */,
          null /* rootCAUUID */);
    } else if (instanceTypeChanged) {
      upgradePodsTask(
          universe.getName(),
          newPlacement,
          masterAddresses,
          curPlacement,
          ServerType.TSERVER,
          newIntent.ybSoftwareVersion,
          universeOverrides,
          azOverrides,
          newNamingStyle,
          isReadOnlyCluster,
          KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
          universe.isYbcEnabled(),
          ybcManager.getStableYbcVersion(),
          PodUpgradeParams.DEFAULT,
          null /* ysqlMajorVersionUpgradeState */,
          null /* rootCAUUID */);
    } else if (masterAddressesChanged) {
      // Update master_addresses flag on Master
      // and tserver_master_addrs flag on tserver without restart.
      createMasterAddressesUpdateTask(
          universe.getUniverseUUID(),
          newCluster,
          newPlacement,
          masterAddresses,
          mastersToAdd,
          mastersToRemove,
          tserversToAdd,
          tserversToRemove);
    }

    // If tservers have been removed, check if some deployments need to be completely
    // removed or scaled down. Also modify the blacklist to untrack deleted pods.
    if (!tserversToRemove.isEmpty()) {
      // Need to unify with DestroyKubernetesUniverse.
      // Using currPlacement, newPlacement we figure out what pods need to be removed. So no need to
      // pass tserversRemoved.
      deletePodsTask(
          universe.getName(),
          curPlacement,
          masterAddresses,
          newPlacement,
          instanceTypeChanged,
          isMultiAZ,
          provider,
          isReadOnlyCluster,
          newNamingStyle,
          universe.isYbcEnabled(),
          /* Will use checksum after last modification, so safe to use */
          supportsNonRestartGflagsUpgrade /* usePreviousGflagsChecksum */);
      Duration sleepBeforeStart =
          confGetter.getConfForScope(
              universe, UniverseConfKeys.ybEditWaitDurationBeforeBlacklistClear);
      if (sleepBeforeStart.compareTo(Duration.ZERO) > 0) {
        createWaitForDurationSubtask(universe, sleepBeforeStart)
            .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
      }

      createModifyBlackListTask(
              null /* addNodes */,
              new ArrayList<>(tserversToRemove) /* removeNodes */,
              false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Update the universe to the new state.
    createSingleKubernetesExecutorTask(
        universe.getName(),
        KubernetesCommandExecutor.CommandType.POD_INFO,
        newPI,
        isReadOnlyCluster);
    if (!tserversToAdd.isEmpty()) {
      installThirdPartyPackagesTaskK8s(
          universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.JWT_JWKS, taskParams());
    }

    if (!mastersToAdd.isEmpty()) {
      // Update the master addresses on the target universes whose source universe belongs to
      // this task.
      createXClusterConfigUpdateMasterAddressesTask();
    }

    return !mastersToAdd.isEmpty();
  }

  private void validateEditParams(Cluster newCluster, Cluster curCluster) {
    // Move this logic to UniverseDefinitionTaskBase.
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
    // Get Universe from DB to confirm latest state.
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    // Perform adds.
    for (int idx = 0; idx < mastersToAdd.size(); idx++) {
      createChangeConfigTasks(mastersToAdd.get(idx), true, subTask);
    }
    // Perform removes.
    for (int idx = 0; idx < mastersToRemove.size(); idx++) {
      // if node is removed in a previous iteration, don't create ChangeConfigTasks
      if (!isFirstTry()) {
        String nodeName = mastersToRemove.get(idx).nodeName;
        log.info("checking if node needs to be removed: " + nodeName);
        if (universe.getNode(nodeName) == null) {
          log.info(
              "Node is already removed, not creating change master config for removal of node ",
              nodeName);
          continue;
        }
      }
      createChangeConfigTasks(mastersToRemove.get(idx), false, subTask);
    }
    // Wait for master leader.
    createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  /*
   * Create master addresses update task
   */
  public void createMasterAddressesUpdateTask(
      UUID universeUUID,
      Cluster newCluster,
      KubernetesPlacement newPlacement,
      String masterAddresses,
      Set<NodeDetails> mastersToAdd,
      Set<NodeDetails> mastersToRemove,
      Set<NodeDetails> tserversToAdd,
      Set<NodeDetails> tserversToRemove) {
    Supplier<String> masterAddressesSupplier = () -> masterAddresses;
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Cluster curPrimaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    String universeOverridesStr = curPrimaryCluster.userIntent.universeOverrides;
    Map<String, String> azOverrides = curPrimaryCluster.userIntent.azOverrides;
    if (azOverrides == null) {
      azOverrides = new HashMap<String, String>();
    }
    boolean isReadOnlyCluster = newCluster.clusterType == ClusterType.ASYNC;
    String softwareVersion = curPrimaryCluster.userIntent.ybSoftwareVersion;

    // .contains() will work for NodeDetails since .equals() is defined.
    Set<NodeDetails> tserversToModify =
        Stream.concat(
                tserversToAdd.stream(),
                universe.getUniverseDetails().getNodesInCluster(newCluster.uuid).stream()
                    .filter(n -> n.isTserver))
            .filter(n -> !tserversToRemove.contains(n))
            .collect(Collectors.toSet());
    if (!isReadOnlyCluster) {
      upgradePodsNonRestart(
          universe.getName(),
          newPlacement,
          masterAddresses,
          ServerType.EITHER,
          softwareVersion,
          universeOverridesStr,
          azOverrides,
          taskParams().useNewHelmNamingStyle,
          isReadOnlyCluster,
          universe.isYbcEnabled(),
          ybcManager.getStableYbcVersion());

      Set<NodeDetails> mastersToModify =
          Stream.concat(
                  mastersToAdd.stream(),
                  universe.getUniverseDetails().getNodesInCluster(newCluster.uuid).stream()
                      .filter(n -> n.isMaster))
              .filter(n -> !mastersToRemove.contains(n))
              .collect(Collectors.toSet());
      // Set flag in memory for master
      createSetFlagInMemoryTasks(
          mastersToModify,
          ServerType.MASTER,
          (node, params) -> {
            params.force = true;
            params.updateMasterAddrs = true;
            params.masterAddrsOverride = masterAddressesSupplier;
          });
      // Set flag in memory for tserver
      createSetFlagInMemoryTasks(
          tserversToModify,
          ServerType.TSERVER,
          (node, params) -> {
            params.force = true;
            params.updateMasterAddrs = true;
            params.masterAddrsOverride = masterAddressesSupplier;
          });
    } else {
      upgradePodsNonRestart(
          universe.getName(),
          newPlacement,
          masterAddresses,
          ServerType.TSERVER,
          softwareVersion,
          universeOverridesStr,
          azOverrides,
          taskParams().useNewHelmNamingStyle,
          isReadOnlyCluster,
          universe.isYbcEnabled(),
          ybcManager.getStableYbcVersion());

      // Set flag in memory for tserver
      createSetFlagInMemoryTasks(
          tserversToModify,
          ServerType.TSERVER,
          (node, params) -> {
            params.force = true;
            params.updateMasterAddrs = true;
            params.masterAddrsOverride = masterAddressesSupplier;
          });
    }
  }

  /*
  Starts up the new pods as requested by the user.
  */
  public void startNewPods(
      String universeName,
      Set<NodeDetails> podsToAdd,
      ServerType serverType,
      PlacementInfo activeZones,
      boolean isReadOnlyCluster,
      String masterAddresses,
      KubernetesPlacement newPlacement,
      KubernetesPlacement currPlacement,
      boolean enableYbc,
      boolean usePreviousGflagsChecksum) {
    createPodsTask(
        universeName,
        newPlacement,
        masterAddresses,
        currPlacement,
        serverType,
        activeZones,
        isReadOnlyCluster,
        enableYbc,
        usePreviousGflagsChecksum);

    createSingleKubernetesExecutorTask(
        universeName,
        KubernetesCommandExecutor.CommandType.POD_INFO,
        activeZones,
        isReadOnlyCluster);

    // Copy the source root certificate to the new pods.
    createTransferXClusterCertsCopyTasks(
        podsToAdd, getUniverse(), SubTaskGroupType.ConfigureUniverse);

    createWaitForServersTasks(podsToAdd, serverType)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    createSwamperTargetUpdateTask(false /* removeFile */);
  }

  protected void createResizeDiskTask(
      String universeName,
      KubernetesPlacement placement,
      UUID clusterUUID,
      String masterAddresses,
      UserIntent userIntent,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      boolean enableYbc,
      String ybcSoftwareVersion,
      boolean tserverDiskSizeChanged,
      boolean masterDiskSizeChanged,
      boolean usePreviousGflagsChecksum) {
    // For non-restart way of master addresses change, we get the previous STS
    // gflags checksum value. For disk resize case, if STS is deleted and task fails,
    // need to have the checksum value available.
    if (usePreviousGflagsChecksum) {
      persistGflagsChecksumInTaskParams(universeName);
      // persist cert checksum in task params for non-restart pods upgrade.
      persistCertChecksumInTaskParams(universeName);
    }
    if (masterDiskSizeChanged && !isReadOnlyCluster) {
      createResizeDiskTask(
          universeName,
          placement,
          masterAddresses,
          userIntent,
          isReadOnlyCluster,
          newNamingStyle,
          enableYbc,
          ybcSoftwareVersion,
          usePreviousGflagsChecksum,
          ServerType.MASTER);
    }
    if (tserverDiskSizeChanged) {
      createResizeDiskTask(
          universeName,
          placement,
          masterAddresses,
          userIntent,
          isReadOnlyCluster,
          newNamingStyle,
          enableYbc,
          ybcSoftwareVersion,
          usePreviousGflagsChecksum,
          ServerType.TSERVER);
    }
    // persist the disk size changes to the universe
    createPersistResizeNodeTask(userIntent, clusterUUID, true /* onlyPersistDeviceInfo */);
  }

  /**
   * Add disk resize tasks for each AZ in given cluster placement. Call this for each cluster of the
   * universe.
   */
  protected void createResizeDiskTask(
      String universeName,
      KubernetesPlacement placement,
      String masterAddresses,
      UserIntent userIntent,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      boolean enableYbc,
      String ybcSoftwareVersion,
      boolean usePreviousGflagsChecksum,
      ServerType serverType) {
    Cluster newCluster =
        isReadOnlyCluster
            ? taskParams().getReadOnlyClusters().get(0)
            : taskParams().getPrimaryCluster();
    Map<UUID, Map<ServerType, String>> perAZGflagsChecksumMap = new HashMap<>();
    if (MapUtils.isNotEmpty(newCluster.getPerAZServerTypeGflagsChecksumMap())) {
      perAZGflagsChecksumMap = newCluster.getPerAZServerTypeGflagsChecksumMap();
    }
    boolean usePreviousCertChecksum = usePreviousGflagsChecksum;
    String certChecksum = null;
    // If cert checksum is not available in universe details, retrieve it from Kubernetes
    if (usePreviousCertChecksum && StringUtils.isNotEmpty(newCluster.getCertChecksum())) {
      certChecksum = newCluster.getCertChecksum();
    }
    // The method to expand disk size is:
    // 1. Delete statefulset without deleting the pods
    // 2. Patch PVC to new disk size
    // 3. Run helm upgrade so that new StatefulSet is created with updated disk size.
    // The newly created statefulSet also claims the already running pods.
    String newDiskSizeGi =
        String.format(
            "%dGi",
            serverType == ServerType.TSERVER
                ? userIntent.deviceInfo.volumeSize
                : userIntent.masterDeviceInfo.volumeSize);
    String softwareVersion = userIntent.ybSoftwareVersion;
    UUID providerUUID = UUID.fromString(userIntent.provider);
    Provider provider = Provider.getOrBadRequest(providerUUID);

    for (Entry<UUID, Map<String, String>> entry : placement.configs.entrySet()) {

      // Subtask groups( ignore Errors is false by default )
      SubTaskGroup validateExpansion =
          createSubTaskGroup(
              KubernetesCheckVolumeExpansion.getSubTaskGroupName(),
              SubTaskGroupType.PreflightChecks);
      SubTaskGroup stsDelete =
          createSubTaskGroup(
              KubernetesCommandExecutor.CommandType.STS_DELETE.getSubTaskGroupName(),
              SubTaskGroupType.ResizingDisk);
      SubTaskGroup pvcExpand =
          createSubTaskGroup(
              KubernetesCommandExecutor.CommandType.PVC_EXPAND_SIZE.getSubTaskGroupName(),
              SubTaskGroupType.ResizingDisk,
              true /* ignoreErrors */);
      SubTaskGroup helmUpgrade =
          createSubTaskGroup(
              KubernetesCommandExecutor.CommandType.HELM_UPGRADE.getSubTaskGroupName(),
              SubTaskGroupType.HelmUpgrade);
      SubTaskGroup postExpansionValidate =
          createSubTaskGroup(
              KubernetesPostExpansionCheckVolume.getSubTaskGroupName(),
              SubTaskGroupType.PostUpdateValidations);

      UUID azUUID = entry.getKey();
      String azName =
          PlacementInfoUtil.isMultiAZ(provider)
              ? AvailabilityZone.getOrBadRequest(azUUID).getCode()
              : null;
      Map<ServerType, String> previousGflagsChecksumMap =
          perAZGflagsChecksumMap.getOrDefault(azUUID, new HashMap<>());
      Map<String, String> azConfig = entry.getValue();
      String namespace =
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix, azName, azConfig, newNamingStyle, isReadOnlyCluster);

      String helmReleaseName =
          KubernetesUtil.getHelmReleaseName(
              taskParams().nodePrefix, universeName, azName, isReadOnlyCluster, newNamingStyle);

      boolean needsExpandPVCInZone =
          KubernetesUtil.needsExpandPVC(
              namespace,
              helmReleaseName,
              serverType == ServerType.TSERVER ? "yb-tserver" : "yb-master" /* appName */,
              newNamingStyle,
              newDiskSizeGi,
              azConfig,
              kubernetesManagerFactory);

      PlacementInfo azPI = new PlacementInfo();
      int rf = placement.masters.getOrDefault(azUUID, 0);
      int numMasters = rf;
      int numNodesInAZ = placement.tservers.getOrDefault(azUUID, 0);
      int numTservers = numNodesInAZ;
      PlacementInfoUtil.addPlacementZone(azUUID, azPI, rf, numNodesInAZ, true);
      // Validate that the StorageClass has allowVolumeExpansion=true
      if (needsExpandPVCInZone) {
        // Only check for volume expansion if we are actually expanding the PVC.
        // Only delete the statefulset if we are actually expanding the PVC.
        // If previous retry already edited the PVC and expanded
        // do it again. We go straight to helm upgrade.
        // This is to make sure we recreate sts with the new disk size to make sure we don't leave
        // pods in orphan state.
        // Add subtask to validateExpansion subtask group
        validateExpansion.addSubTask(
            createTaskToValidateExpansion(
                universeName,
                azConfig,
                azName,
                isReadOnlyCluster,
                newNamingStyle,
                providerUUID,
                serverType));
        // create the three tasks to update volume size
        // Add subtask to stsDelete subtask group
        stsDelete.addSubTask(
            getSingleKubernetesExecutorTaskForServerTypeTask(
                universeName,
                KubernetesCommandExecutor.CommandType.STS_DELETE,
                azPI,
                azName,
                masterAddresses,
                softwareVersion,
                serverType,
                azConfig,
                0 /* masterPartition */,
                0 /* tserverPartition */,
                null /* universeOverrides */,
                null /* azOverrides */,
                isReadOnlyCluster,
                null /* podName */,
                newDiskSizeGi,
                enableYbc,
                ybcSoftwareVersion,
                false /* usePreviousGflagsChecksum */,
                null /* previousGflagsChecksumMap */,
                false /* usePreviousCertChecksum */,
                null /* previousCertChecksum */,
                false /* useNewMasterDiskSize */,
                false /* useNewTserverDiskSize */,
                null /* ysqlMajorVersionUpgradeState */,
                null /* rootCAUUID */));
        // Add subtask to pvcExpand subtask group
        pvcExpand.addSubTask(
            getSingleKubernetesExecutorTaskForServerTypeTask(
                universeName,
                KubernetesCommandExecutor.CommandType.PVC_EXPAND_SIZE,
                azPI,
                azName,
                masterAddresses,
                softwareVersion,
                serverType,
                azConfig,
                0 /* masterPartition */,
                0 /* tserverPartition */,
                null /* universeOverrides */,
                null /* azOverrides */,
                isReadOnlyCluster,
                null /* podName */,
                newDiskSizeGi,
                enableYbc,
                ybcSoftwareVersion,
                false /* usePreviousGflagsChecksum */,
                null /* previousGflagsChecksumMap */,
                false /* usePreviousCertChecksum */,
                null /* previousCertChecksum */,
                false /* useNewMasterDiskSize */,
                false /* useNewTserverDiskSize */,
                null /* ysqlMajorVersionUpgradeState */,
                null /* rootCAUUID */));
      }
      // This helm upgrade will only create the new statefulset with the new disk size, nothing else
      // should change here and this is idempotent, since its a helm_upgrade.

      // universeOverrides is a string
      // azOverrides is a map because it comes from AZ
      // If we are here we must have primary cluster defined.
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      Map<String, Object> universeOverrides =
          HelmUtils.convertYamlToMap(primaryCluster.userIntent.universeOverrides);
      Map<String, String> azOverrides = primaryCluster.userIntent.azOverrides;
      if (azOverrides == null) {
        azOverrides = new HashMap<String, String>();
      }
      Map<String, Object> azOverridesPerAZ = HelmUtils.convertYamlToMap(azOverrides.get(azName));
      if (azOverridesPerAZ == null) {
        azOverridesPerAZ = new HashMap<String, Object>();
      }
      // Add subtask to helmUpgrade subtask group
      // Master and Tserver partition are equal to num_pods for PVC recreate helm upgrade.
      // They will be reset on next rolling upgrade for both server types.
      // This is to prevent unexpected restarts due to diverged values.
      helmUpgrade.addSubTask(
          getSingleKubernetesExecutorTaskForServerTypeTask(
              universeName,
              KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
              azPI,
              azName,
              masterAddresses,
              softwareVersion,
              serverType,
              azConfig,
              numMasters /* masterPartition */,
              numTservers /* tserverPartition */,
              universeOverrides,
              azOverridesPerAZ,
              isReadOnlyCluster,
              null /* podName */,
              newDiskSizeGi,
              enableYbc,
              ybcSoftwareVersion,
              usePreviousGflagsChecksum,
              previousGflagsChecksumMap,
              usePreviousCertChecksum,
              certChecksum,
              true /* useNewMasterDiskSize */,
              serverType == ServerType.TSERVER ? true : false /* useNewTserverDiskSize */,
              null /* ysqlMajorVersionUpgradeState */,
              null /* rootCAUUID */));
      // Add subtask to postExpansionValidate subtask group
      postExpansionValidate.addSubTask(
          createPostExpansionValidateTask(
              universeName,
              azConfig,
              azName,
              isReadOnlyCluster,
              newNamingStyle,
              providerUUID,
              newDiskSizeGi,
              serverType));

      // Add all subtasks to runnable
      if (validateExpansion.getSubTaskCount() > 0) {
        getRunnableTask().addSubTaskGroup(validateExpansion);
        getRunnableTask().addSubTaskGroup(stsDelete);
        getRunnableTask().addSubTaskGroup(pvcExpand);
        getRunnableTask().addSubTaskGroup(helmUpgrade);
        getRunnableTask().addSubTaskGroup(postExpansionValidate);
      }
    }
  }

  private KubernetesCheckVolumeExpansion createTaskToValidateExpansion(
      String universeName,
      Map<String, String> config,
      String azName,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      UUID providerUUID,
      ServerType serverType) {
    KubernetesCheckVolumeExpansion.Params params = new KubernetesCheckVolumeExpansion.Params();
    params.config = config;
    params.newNamingStyle = newNamingStyle;
    if (config != null) {
      params.namespace =
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              azName,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    params.providerUUID = providerUUID;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            azName,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle);
    params.serverType = serverType;
    KubernetesCheckVolumeExpansion task = createTask(KubernetesCheckVolumeExpansion.class);
    task.initialize(params);
    return task;
  }

  private KubernetesPostExpansionCheckVolume createPostExpansionValidateTask(
      String universeName,
      Map<String, String> config,
      String azName,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      UUID providerUUID,
      String newDiskSizeGi,
      ServerType serverType) {
    KubernetesPostExpansionCheckVolume.Params params =
        new KubernetesPostExpansionCheckVolume.Params();
    params.config = config;
    params.newNamingStyle = newNamingStyle;
    if (config != null) {
      params.namespace =
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix,
              azName,
              config,
              taskParams().useNewHelmNamingStyle,
              isReadOnlyCluster);
    }
    params.providerUUID = providerUUID;
    params.newDiskSizeGi = newDiskSizeGi;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            taskParams().nodePrefix,
            universeName,
            azName,
            isReadOnlyCluster,
            taskParams().useNewHelmNamingStyle);
    params.serverType = serverType;
    KubernetesPostExpansionCheckVolume task = createTask(KubernetesPostExpansionCheckVolume.class);
    task.initialize(params);
    return task;
  }

  protected void persistGflagsChecksumInTaskParams(String universeName) {
    if (MapUtils.isNotEmpty(
        taskParams().getPrimaryCluster().getPerAZServerTypeGflagsChecksumMap())) {
      return;
    }
    UUID universeUUID = taskParams().getUniverseUUID();
    Universe universe = Universe.getOrBadRequest(universeUUID);
    for (Cluster newCluster : taskParams().clusters) {
      // Use existing cluster AZs to persist here.
      Map<UUID, Map<ServerType, String>> perAZGflagsChecksumMap =
          getPerAZGflagsChecksumMap(universeName, universe.getCluster(newCluster.uuid));
      if (MapUtils.isNotEmpty(perAZGflagsChecksumMap)) {
        newCluster.setPerAZServerTypeGflagsChecksumMap(perAZGflagsChecksumMap);
        log.debug("Persisting gflags checksum");
      }
    }
    TaskInfo.updateInTxn(getUserTaskUUID(), tf -> tf.setTaskParams(Json.toJson(taskParams())));
  }

  protected void persistCertChecksumInTaskParams(String universeName) {
    if (StringUtils.isNotEmpty(taskParams().getPrimaryCluster().getCertChecksum())) {
      return;
    }
    UUID universeUUID = taskParams().getUniverseUUID();
    Universe universe = Universe.getOrBadRequest(universeUUID);
    String certChecksum = getCertChecksum(universe);
    taskParams().getPrimaryCluster().setCertChecksum(certChecksum);
    log.debug("Persisting cert checksum: {}", certChecksum);
    for (Cluster newCluster : taskParams().clusters) {
      newCluster.setCertChecksum(certChecksum);
    }
    TaskInfo.updateInTxn(getUserTaskUUID(), tf -> tf.setTaskParams(Json.toJson(taskParams())));
  }

  private void createCheckNodeSafeToDeleteTasks(
      UUID universeUUID, Set<NodeDetails> mastersToRemove, Set<NodeDetails> tserversToRemove) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    List<NodeDetails> checkNodesSafeToDelete =
        Stream.concat(mastersToRemove.stream(), tserversToRemove.stream())
            .filter(n -> universe.getNode(n.getNodeName()) != null)
            .collect(Collectors.toList());
    if (CollectionUtils.isNotEmpty(checkNodesSafeToDelete)) {
      createCheckNodeSafeToDeleteTasks(universe, checkNodesSafeToDelete);
    }
  }

  public static boolean checkEditKubernetesRerunAllowed(TaskInfo placementModificationTaskInfo) {
    UniverseDefinitionTaskParams placementTaskParams =
        Json.fromJson(
            placementModificationTaskInfo.getTaskParams(), UniverseDefinitionTaskParams.class);
    Universe universe = Universe.getOrBadRequest(placementTaskParams.getUniverseUUID());
    UniverseDefinitionTaskParams currentUniverseParams = universe.getUniverseDetails();
    for (Cluster newCluster : placementTaskParams.clusters) {
      Cluster currCluster = currentUniverseParams.getClusterByUuid(newCluster.uuid);

      UserIntent newIntent = newCluster.userIntent, currIntent = currCluster.userIntent;
      PlacementInfo newPI = newCluster.placementInfo, curPI = currCluster.placementInfo;

      // Not allowing re-run if disk size change was attempted
      if (newIntent.deviceInfo.volumeSize != currIntent.deviceInfo.volumeSize) {
        return false;
      }

      // Not allowing re-run if any kind of Cluster configuration change was attempted
      boolean isReadOnlyCluster = newCluster.clusterType.equals(ClusterType.ASYNC);
      if (!isReadOnlyCluster) {
        int numTotalMasters = newIntent.replicationFactor;
        PlacementInfoUtil.selectNumMastersAZ(
            newPI, numTotalMasters, PlacementInfoUtil.getZonesForMasters(newCluster));
      }
      KubernetesPlacement newPlacement = new KubernetesPlacement(newPI, isReadOnlyCluster),
          curPlacement = new KubernetesPlacement(curPI, isReadOnlyCluster);

      boolean mastersChanged = !curPlacement.masters.equals(newPlacement.masters);
      boolean tserversChanged = !curPlacement.tservers.equals(newPlacement.tservers);

      if (mastersChanged || tserversChanged) {
        return false;
      }
    }
    return true;
  }
}
