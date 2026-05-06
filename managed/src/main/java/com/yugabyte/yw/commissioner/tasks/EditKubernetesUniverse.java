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

import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallThirdPartySoftwareK8s;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckNumPod;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckVolumeExpansion;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.FullMoveParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesPostExpansionCheckVolume;
import com.yugabyte.yw.common.KubernetesFullMoveReplicas;
import com.yugabyte.yw.common.KubernetesFullMoveReplicas.KubernetesFullMoveReplica;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import java.util.function.BiFunction;
import java.util.function.Consumer;
import java.util.function.Predicate;
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
      validateStorageClassesOnEdit();
    }
    if (universe.getUniverseDetails().getPrimaryCluster().isGeoPartitioned()
        && universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL) {
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      createTablespaceValidationOnRemoveTask(primaryCluster.uuid);
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

      Provider provider = Util.getSingleProvider(primaryCluster);

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
    Provider provider = Util.getSingleProvider(primaryCluster);
    boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

    Map<UUID, Set<NodeDetails>> mastersToAddAzMap =
        getPodsToAdd(newPlacement, curPlacement, ServerType.MASTER, isMultiAZ, isReadOnlyCluster);
    Map<UUID, Set<NodeDetails>> mastersToRemoveAzMap =
        getPodsToRemove(
            newPlacement, curPlacement, ServerType.MASTER, universe, isMultiAZ, isReadOnlyCluster);
    Map<UUID, Set<NodeDetails>> tserversToAddAzMap =
        getPodsToAdd(newPlacement, curPlacement, ServerType.TSERVER, isMultiAZ, isReadOnlyCluster);
    Map<UUID, Set<NodeDetails>> tserversToRemoveAzMap =
        getPodsToRemove(
            newPlacement, curPlacement, ServerType.TSERVER, universe, isMultiAZ, isReadOnlyCluster);

    Set<UUID> fullMoveMasterAZs =
        Sets.intersection(mastersToAddAzMap.keySet(), mastersToRemoveAzMap.keySet());
    Set<UUID> fullMoveTserverAZs =
        Sets.intersection(tserversToAddAzMap.keySet(), tserversToRemoveAzMap.keySet());

    Map<UUID, Pair<Boolean, Boolean>> azToDiskSizeChangeMap =
        canResizeDisk(
            curPlacement, newPlacement, newIntent, curIntent, taskParams().nodeDetailsSet);
    if (!azToDiskSizeChangeMap.isEmpty()) {
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
          azToDiskSizeChangeMap,
          supportsNonRestartGflagsUpgrade /* usePreviousGflagsChecksum */,
          fullMoveMasterAZs,
          fullMoveTserverAZs);
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

    PlacementInfo activeZones = new PlacementInfo();
    for (UUID currAZs : curPlacement.configs.keySet()) {
      PlacementInfoUtil.addPlacementZone(currAZs, activeZones);
    }

    // There are 2 cases to handle:
    // 1. When net new AZ is added/deleted: Pods in these AZs are only in ToBeAdded/ToBeDeleted
    //    state. For this, we will follow the "handle all in once" strategy. This means, all pods
    //    are created once, we wait for data migration, finally all pods are removed at once.
    // 2. For full move case: Pods in these AZs are in both ToBeAdded/ToBeDeleted state.
    //    This means, the existing pods should be removed and should be replaced by new pods. Note
    //    that the number of pods to be removed and re-added can be distinct, and the full move
    // logic
    //    handles this case. The full move logic supports batching.

    // The flow in general is:
    //  1. Add non-full move master pods( AZs not undergoing full move ).
    //  2. Full move:
    //        a. Perform full move on master pods( AZs undergoing full move ).
    //        b. Perform full move on tserver pods( adding new + deleting old pods ).
    //        c. Perform data migration, master address update, deleting old pods for AZs
    //           undergoing full move.
    //  4. Add non-full move tserver pods.
    //  5. Data migration.
    //  6. Delete old pods.

    // Non-full move AZs handling
    BiFunction<Map<UUID, Set<NodeDetails>>, Set<UUID>, Set<NodeDetails>>
        nonFullMoveAZsPodsBiFunction =
            (podsToAzMap, skipAZs) ->
                podsToAzMap.entrySet().stream()
                    .filter(e -> CollectionUtils.isEmpty(skipAZs) || !skipAZs.contains(e.getKey()))
                    .flatMap(e -> e.getValue().stream())
                    .collect(Collectors.toSet());
    Set<NodeDetails> mastersToAdd =
        nonFullMoveAZsPodsBiFunction.apply(mastersToAddAzMap, fullMoveMasterAZs);
    Set<NodeDetails> mastersToRemove =
        nonFullMoveAZsPodsBiFunction.apply(mastersToRemoveAzMap, fullMoveMasterAZs);
    Set<NodeDetails> tserversToAdd =
        nonFullMoveAZsPodsBiFunction.apply(tserversToAddAzMap, fullMoveTserverAZs);
    Set<NodeDetails> tserversToRemove =
        nonFullMoveAZsPodsBiFunction.apply(tserversToRemoveAzMap, fullMoveTserverAZs);

    masterAddressesChanged =
        CollectionUtils.isNotEmpty(mastersToAdd) || CollectionUtils.isNotEmpty(fullMoveMasterAZs);
    if (!mastersToAdd.isEmpty()) {
      // Bring up new masters and update the configs.
      // No need to check mastersToRemove as total number of masters is invariant.
      // Handle previously executed 'Add' operations on master nodes. To avoid
      // re-initializing these masters, a separate local structure is used for
      // storing a filtered list of uninitialized masters. Note: 'mastersToAdd'
      // is not modified directly to maintain control flow for downstream code.

      Set<NodeDetails> newMasters = filterMastersToAdd(mastersToAdd, universe);
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
          supportsNonRestartGflagsUpgrade && !restartAllPods /* usePreviousGflagsChecksum */,
          fullMoveMasterAZs);

      // Update master addresses to the latest required ones,
      // We use the original unfiltered mastersToAdd which is determined from pi.
      createMoveMasterTasks(new ArrayList<>(mastersToAdd), new ArrayList<>(mastersToRemove));
    }

    if (CollectionUtils.isNotEmpty(fullMoveMasterAZs)
        || CollectionUtils.isNotEmpty(fullMoveTserverAZs)) {
      if (CollectionUtils.isNotEmpty(fullMoveMasterAZs)) {
        // Ybc is not present on master-only nodes currently
        createFullMoveTasks(
            universe,
            curPlacement,
            newPlacement,
            fullMoveTserverAZs,
            fullMoveMasterAZs,
            ServerType.MASTER,
            curIntent,
            newIntent,
            isReadOnlyCluster,
            newNamingStyle,
            masterAddresses,
            false /* enableYbc */,
            null /* ybcSoftwareVersion */);
      }
      if (CollectionUtils.isNotEmpty(fullMoveTserverAZs)) {
        createFullMoveTasks(
            universe,
            curPlacement,
            newPlacement,
            fullMoveTserverAZs,
            fullMoveMasterAZs,
            ServerType.TSERVER,
            curIntent,
            newIntent,
            isReadOnlyCluster,
            newNamingStyle,
            masterAddresses,
            universe.isYbcEnabled(),
            ybcManager.getStableYbcVersion());
      }
      if (CollectionUtils.isNotEmpty(fullMoveMasterAZs) || !mastersToAdd.isEmpty()) {
        BiFunction<Map<UUID, Set<NodeDetails>>, Set<UUID>, Set<NodeDetails>>
            fullMoveAZsPodsBiFunction =
                (podsToAzMap, filterAZs) ->
                    podsToAzMap.entrySet().stream()
                        .filter(e -> filterAZs.contains(e.getKey()))
                        .flatMap(e -> e.getValue().stream())
                        .collect(Collectors.toSet());
        Set<NodeDetails> fullMoveMastersToAdd =
            fullMoveAZsPodsBiFunction.apply(mastersToAddAzMap, fullMoveMasterAZs);
        Set<NodeDetails> fullMoveMastersToRemove =
            fullMoveAZsPodsBiFunction.apply(mastersToRemoveAzMap, fullMoveMasterAZs);
        Set<NodeDetails> fullMoveTserversToAdd =
            fullMoveAZsPodsBiFunction.apply(tserversToAddAzMap, fullMoveTserverAZs);
        Set<NodeDetails> fullMoveTserversToRemove =
            fullMoveAZsPodsBiFunction.apply(tserversToRemoveAzMap, fullMoveTserverAZs);
        Set<UUID> skipMasterAZs =
            Sets.difference(newPlacement.placementInfo.getAllAZUUIDs(), fullMoveMasterAZs);
        Set<UUID> skipTserverAZs =
            Sets.difference(newPlacement.placementInfo.getAllAZUUIDs(), fullMoveTserverAZs);
        // Create master address update task for all full move AZ pods.
        createMasterAddressesUpdateTask(
            universe.getUniverseUUID(),
            newCluster,
            newPlacement,
            masterAddresses,
            fullMoveMastersToAdd,
            fullMoveMastersToRemove,
            fullMoveTserversToAdd,
            fullMoveTserversToRemove,
            skipMasterAZs,
            skipTserverAZs,
            false /* includeUniverseNodes */);
      }
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
              && !(restartAllPods || instanceTypeChanged) /* usePreviousGflagsChecksum */,
          fullMoveTserverAZs);

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
          null /* rootCAUUID */,
          fullMoveMasterAZs);

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
          null /* rootCAUUID */,
          fullMoveTserverAZs);
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
          null /* rootCAUUID */,
          fullMoveTserverAZs);
    } else if (masterAddressesChanged) {
      // Update master_addresses flag on Master
      // and tserver_master_addrs flag on tserver without restart.
      // Here, skipMasters and skipTservers refer to nodes where master address change
      // should not apply. It should include all removed nodes( irrespective of full move/edit ).
      Set<NodeDetails> skipMasters =
          mastersToRemoveAzMap.entrySet().stream()
              .flatMap(e -> e.getValue().stream())
              .collect(Collectors.toSet());
      Set<NodeDetails> skipTservers =
          tserversToRemoveAzMap.entrySet().stream()
              .flatMap(e -> e.getValue().stream())
              .collect(Collectors.toSet());
      createMasterAddressesUpdateTask(
          universe.getUniverseUUID(),
          newCluster,
          newPlacement,
          masterAddresses,
          mastersToAdd,
          skipMasters,
          tserversToAdd,
          skipTservers,
          fullMoveMasterAZs,
          fullMoveTserverAZs,
          true /* includeUniverseNodes */);
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
          supportsNonRestartGflagsUpgrade /* usePreviousGflagsChecksum */,
          fullMoveTserverAZs);
    }

    // Clear blacklist
    if (!tserversToRemove.isEmpty() || CollectionUtils.isNotEmpty(fullMoveTserverAZs)) {
      Duration sleepBeforeStart =
          confGetter.getConfForScope(
              universe, UniverseConfKeys.ybEditWaitDurationBeforeBlacklistClear);
      if (sleepBeforeStart.compareTo(Duration.ZERO) > 0) {
        createWaitForDurationSubtask(universe, sleepBeforeStart)
            .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
      }
      // Clear blacklist for all pods
      Set<NodeDetails> removedTservers =
          nonFullMoveAZsPodsBiFunction.apply(tserversToRemoveAzMap, null /* fullMoveAZs */);

      createModifyBlackListTask(
              null /* addNodes */,
              new ArrayList<>(removedTservers) /* removeNodes */,
              false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Update the universe to the new state.
    createSingleKubernetesExecutorTask(
        universe.getName(),
        KubernetesCommandExecutor.CommandType.POD_INFO,
        newPI,
        isReadOnlyCluster);
    if (!tserversToAdd.isEmpty() || CollectionUtils.isNotEmpty(fullMoveTserverAZs)) {
      installThirdPartyPackagesTaskK8s(
          universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.JWT_JWKS, taskParams());
    }

    if (masterAddressesChanged) {
      // Update the master addresses on the target universes whose source universe belongs to
      // this task.
      createXClusterConfigUpdateMasterAddressesTask();
    }

    return masterAddressesChanged;
  }

  private Set<NodeDetails> filterMastersToAdd(Set<NodeDetails> mastersToAdd, Universe universe) {
    Set<NodeDetails> newMasters = new HashSet<>(mastersToAdd);
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
    return newMasters;
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
      Set<NodeDetails> tserversToRemove,
      Set<UUID> skipMasterAZs,
      Set<UUID> skipTserverAZs,
      boolean includeUniverseNodes) {
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
                includeUniverseNodes
                    ? universe.getUniverseDetails().getNodesInCluster(newCluster.uuid).stream()
                        .filter(n -> n.isTserver)
                    : Stream.empty())
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
          ybcManager.getStableYbcVersion(),
          null /* ysqlMajorVersionUpgradeState */,
          null /* rootCAUUID */,
          skipMasterAZs,
          skipTserverAZs);

      Set<NodeDetails> mastersToModify =
          Stream.concat(
                  mastersToAdd.stream(),
                  includeUniverseNodes
                      ? universe.getUniverseDetails().getNodesInCluster(newCluster.uuid).stream()
                          .filter(n -> n.isMaster)
                      : Stream.empty())
              .filter(n -> !mastersToRemove.contains(n))
              .collect(Collectors.toSet());
      // Set flag in memory for master
      if (CollectionUtils.isNotEmpty(mastersToModify)) {
        createSetFlagInMemoryTasks(
            mastersToModify,
            ServerType.MASTER,
            (node, params) -> {
              params.force = true;
              params.updateMasterAddrs = true;
              params.masterAddrsOverride = masterAddressesSupplier;
            });
      }
      // Set flag in memory for tserver
      if (CollectionUtils.isNotEmpty(tserversToModify)) {
        createSetFlagInMemoryTasks(
            tserversToModify,
            ServerType.TSERVER,
            (node, params) -> {
              params.force = true;
              params.updateMasterAddrs = true;
              params.masterAddrsOverride = masterAddressesSupplier;
            });
      }
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
          ybcManager.getStableYbcVersion(),
          null /* ysqlMajorVersionUpgradeState */,
          null /* rootCAUUID */,
          skipMasterAZs,
          skipTserverAZs);

      // Set flag in memory for tserver
      if (CollectionUtils.isNotEmpty(tserversToModify)) {
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
      boolean usePreviousGflagsChecksum,
      Set<UUID> skipAZs) {
    createPodsTask(
        universeName,
        newPlacement,
        masterAddresses,
        currPlacement,
        serverType,
        activeZones,
        isReadOnlyCluster,
        enableYbc,
        usePreviousGflagsChecksum,
        skipAZs);

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

  /**
   * Create subtasks for full move operation within AZs. This supports batched full move for tserver
   * pods.
   *
   * <p>Creates the following subtasks for each batch iteration (for both MASTER and TSERVER):
   *
   * <ul>
   *   <li>Full Move Helm Upgrade - Creates new pods via helm upgrade with full move params
   *   <li>Wait for Pods (Pre-Delete) - Waits for pods to be ready before deletion
   *   <li>Check Node Safe to Delete - Validates nodes are safe to delete
   *   <li>Pod Delete Helm Upgrade - Deletes old pods via helm upgrade
   *   <li>Wait for Pods (Post-Delete) - Waits for pods after deletion
   *   <li>POD_INFO - Updates pod information
   *   <li>Transfer XCluster Certs - Copies certificates for xcluster replication
   *   <li>Wait for Servers - Waits for servers to be ready
   *   <li>Swamper Target Update - Updates monitoring targets
   * </ul>
   *
   * <p>Additional subtasks for MASTER:
   *
   * <ul>
   *   <li>Move Master Tasks - Updates master configuration (add/remove masters)
   * </ul>
   *
   * <p>Additional subtasks for TSERVER:
   *
   * <ul>
   *   <li>Install YBC - Installs YBC on newly added pods (if YBC is enabled)
   *   <li>Wait for YBC Server - Waits for YBC server to be ready
   *   <li>Placement Info Task - Updates blacklist on master leader
   *   <li>Wait for Data Move - Waits for data migration to complete
   *   <li>Wait for Load Balance - Waits for load balancing (if configured)
   * </ul>
   *
   * <p>Final subtasks (after all batches):
   *
   * <ul>
   *   <li>Final Helm Upgrade - Applies new placement and device info to new StatefulSet
   *   <li>Garbage Collect Master Volumes - Deletes unused master PVCs (MASTER only, primary cluster
   *       only)
   * </ul>
   *
   * @param universe The universe being edited
   * @param curPlacement Current Kubernetes placement configuration
   * @param newPlacement New Kubernetes placement configuration
   * @param tsFullMoveAZs Set of AZ UUIDs undergoing full move for tservers
   * @param masterFullMoveAZs Set of AZ UUIDs undergoing full move for masters
   * @param serverType Server type (MASTER or TSERVER)
   * @param curIntent Current user intent
   * @param newIntent New user intent
   * @param isReadOnlyCluster Whether this is a read-only cluster
   * @param newNamingStyle Whether to use new helm naming style
   * @param masterAddresses Master addresses string
   * @param enableYbc Whether YBC is enabled
   * @param ybcSoftwareVersion YBC software version
   */
  protected void createFullMoveTasks(
      Universe universe,
      KubernetesPlacement curPlacement,
      KubernetesPlacement newPlacement,
      Set<UUID> tsFullMoveAZs,
      Set<UUID> masterFullMoveAZs,
      ServerType serverType,
      UserIntent curIntent,
      UserIntent newIntent,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      String masterAddresses,
      boolean enableYbc,
      String ybcSoftwareVersion) {
    String universeName = universe.getName();
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }
    UUID clusterUUID =
        isReadOnlyCluster ? taskParams().getReadOnlyClusters().get(0).uuid : primaryCluster.uuid;
    String providerStr =
        isReadOnlyCluster
            ? taskParams().getReadOnlyClusters().get(0).userIntent.provider
            : primaryCluster.userIntent.provider;
    Provider provider = Provider.getOrBadRequest(UUID.fromString(providerStr));
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
    String nodePrefix = taskParams().nodePrefix;

    Map<UUID, Integer> serversToUpdate =
        (serverType == ServerType.TSERVER ? newPlacement.tservers : newPlacement.masters)
            .entrySet().stream()
                .filter(
                    e ->
                        (serverType == ServerType.TSERVER ? tsFullMoveAZs : masterFullMoveAZs)
                            .contains(e.getKey()))
                .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));

    Map<String, Object> universeOverrides =
        HelmUtils.convertYamlToMap(primaryCluster.userIntent.universeOverrides);
    Map<String, String> azsOverrides = primaryCluster.userIntent.azOverrides;

    int batchSize = 0; // Batch size for master will be 0
    if (serverType == ServerType.TSERVER) {
      batchSize =
          runtimeConfigFactory
              .forUniverse(getUniverse())
              .getInt(UniverseConfKeys.fullMoveRollBatchSize.getKey());
    }

    // Map of <Integer, SubTaskGroup> and <Integer, List> is done to add support
    // for grouping various subtasks in such a way that we can support batching.
    // All same index items will be grouped in the same SubTaskGroup kind.
    Map<Integer, List<NodeDetails>> serversToAddPerSubtaskGroupMap = new HashMap<>();
    Map<Integer, List<NodeDetails>> serversToRemovePerSubtaskGroupMap = new HashMap<>();
    Map<Integer, List<String>> podNamesToAddPerSubtaskGroupMap = new HashMap<>();
    Map<Integer, List<String>> podNamesToRemovePerSubtaskGroupMap = new HashMap<>();
    Map<Integer, SubTaskGroup> fullMoveSubTaskGroupsMap = new HashMap<>();
    Map<Integer, SubTaskGroup> podsDeleteSubTaskGroupsMap = new HashMap<>();
    Map<Integer, SubTaskGroup> waitForPodsPreDeleteSubTaskGroupsMap = new HashMap<>();
    Map<Integer, SubTaskGroup> waitForPodsPostDeleteSubTaskGroupsMap = new HashMap<>();
    SubTaskGroup pvcDeletes =
        createSubTaskGroup(
            KubernetesCommandExecutor.CommandType.VOLUME_DELETE_SHELL_MODE_MASTER
                .getSubTaskGroupName());
    pvcDeletes.setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
    SubTaskGroup helmUpgrades =
        createSubTaskGroup(
            KubernetesCommandExecutor.CommandType.HELM_UPGRADE.getSubTaskGroupName());
    helmUpgrades.setSubTaskGroupType(SubTaskGroupType.HelmUpgrade);

    // Only generate the list of subtasks and pods to add/remove in each iteration across AZs.
    // Adding to running task is done later as we need to handle group some subtasks together.
    // Flow for subtasks generation:
    //  1. For each AZ:
    //        a. Run replicas iterator which returns pods to remove from old STS, add to new STS.
    //        b. Create full move params from pods to add( do not remove pods yet ).
    //        c. Add full move subtask to current index. Add same index subtasks will run together.
    //        d. Collect pods to add, pods to remove in this interation to be used later.
    //        e. Create subtask to delete pods in batch.
    //  2. After collecting all batched pods, all pods belonging to same index across AZs are
    //     processed together. This means, they will either be added to same subtask: Master change
    //     config, WaitForYbcServer, CheckNodeSafeToDelete etc OR they will be put in the same
    //     SubTaskGroup to be processed in parallel( Full Move, HelmUpgrade to delete etc ).
    for (Entry<UUID, Integer> entry : serversToUpdate.entrySet()) {
      UUID azUUID = entry.getKey();
      String azCode = isMultiAz ? AvailabilityZone.get(azUUID).getCode() : null;

      String azOverridesStr = "";
      if (azsOverrides != null) {
        azOverridesStr = azsOverrides.get(PlacementInfoUtil.getAZNameFromUUID(provider, azUUID));
      }
      Map<String, Object> azOverrides = HelmUtils.convertYamlToMap(azOverridesStr);

      int newNumMasters = newPlacement.masters.getOrDefault(azUUID, 0);
      int newNumTservers = newPlacement.tservers.getOrDefault(azUUID, 0);

      int currNumMasters = curPlacement.masters.getOrDefault(azUUID, 0);
      int currNumTservers = curPlacement.tservers.getOrDefault(azUUID, 0);

      PlacementAZ newPlacementAZ = newPlacement.placementInfo.findByAZUUID(azUUID);
      PlacementAZ oldPlacementAZ = curPlacement.placementInfo.findByAZUUID(azUUID);

      // The DISK logic( params handling ) below is predicated on the fact that master is
      // handled first
      Integer tsOldDiskSize = null, masterOldDiskSize = null;
      if (taskParams().getClusterByUuid(clusterUUID).getOriginalDiskSize() != null) {
        int oldTsSize =
            taskParams()
                .getClusterByUuid(clusterUUID)
                .getOriginalDiskSize()
                .get(azUUID)
                .get(ServerType.TSERVER);
        int oldMasterSize =
            taskParams()
                .getClusterByUuid(clusterUUID)
                .getOriginalDiskSize()
                .get(azUUID)
                .get(ServerType.MASTER);
        // Disk size( this needs special handling as we persist new disk size if resize
        // is combined with full move )
        if (serverType == ServerType.MASTER) {
          // For MASTER case: current STS should continue to use old disk size for master
          // and for Tserver: use Old disk size if the AZ has to undergo full move
          // for Tserver.
          masterOldDiskSize = oldMasterSize;
          if (CollectionUtils.isNotEmpty(tsFullMoveAZs) && tsFullMoveAZs.contains(azUUID)) {
            tsOldDiskSize = oldTsSize;
          }
        } else {
          // For TSERVER case: always use new disk size for master.
          // For Tserver: use old disk size for current STS
          tsOldDiskSize = oldTsSize;
        }
      }

      Map<String, String> config = newPlacement.configs.get(azUUID);

      int index = 0, masterPartition = 0, tserverPartition = 0;

      for (KubernetesFullMoveReplica replicas :
          KubernetesFullMoveReplicas.iterable(
              batchSize,
              oldPlacementAZ,
              newPlacementAZ,
              serverType,
              (part, sIndex) ->
                  KubernetesUtil.getPodName(
                      part,
                      azCode,
                      serverType,
                      nodePrefix,
                      isMultiAz,
                      newNamingStyle,
                      universeName,
                      isReadOnlyCluster,
                      sIndex),
              (part, sIndex) ->
                  KubernetesUtil.getKubernetesNodeName(
                      part, azCode, serverType, isMultiAz, isReadOnlyCluster, sIndex))) {

        // If current batch is already processed in previous run, we need to skip it
        int savedBatchIndex =
            serverType == ServerType.TSERVER
                ? taskParams().getClusterByUuid(clusterUUID).getCurrentTsFullMoveBatchIndex()
                : taskParams().getClusterByUuid(clusterUUID).getCurrentMasterFullMoveBatchIndex();
        if (savedBatchIndex >= index) {
          index++;
          continue;
        }

        // For deviceInfo params:
        // useNewMasterDeviceInfo: True for TSERVER, false for MASTER serverType, as for TSERVER
        // masters have been moved to new device Info.
        // useNewTserverDeviceInfo: False if TSERVER, true if MASTER and TSERVER is not undergoing
        // full move. This is same for full move subtask and batch pods delete subtask.
        boolean useNewMasterDeviceInfo = serverType == ServerType.TSERVER ? true : false;
        boolean useNewTserverDeviceInfo =
            serverType == ServerType.TSERVER
                ? false
                : CollectionUtils.isEmpty(tsFullMoveAZs) || !tsFullMoveAZs.contains(azUUID);

        FullMoveParams moveParams = new FullMoveParams();
        // Add full move subtask if new node added in this iteration
        if (CollectionUtils.isNotEmpty(replicas.newNodeList)) {
          // Master replicas will anyways be added together as batchSize will be 0.
          moveParams.masterReplicas = newPlacementAZ.replicationFactor;
          moveParams.tsReplicas = serverType == ServerType.TSERVER ? replicas.newReplicas : 0;
          moveParams.masterStsEndIndex = newPlacementAZ.masterStsIndex;
          moveParams.masterStsStartIndex =
              serverType == ServerType.TSERVER
                  ? newPlacementAZ.masterStsIndex
                  : oldPlacementAZ.masterStsIndex;
          moveParams.tsStsEndIndex =
              serverType == ServerType.TSERVER
                  ? newPlacementAZ.tsStsIndex
                  : oldPlacementAZ.tsStsIndex;
          moveParams.tsStsStartIndex = oldPlacementAZ.tsStsIndex;
          moveParams.tsPartition = serverType == ServerType.TSERVER ? replicas.newReplicas : 0;
          moveParams.masterPartition = newPlacementAZ.replicationFactor;

          // For full move temp PlacementInfo, we don't need stsIndex as
          // stsIndex comes from moveOp( full move params ).
          PlacementInfo tempPI = new PlacementInfo();
          PlacementInfoUtil.addPlacementZone(azUUID, tempPI);
          tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ =
              serverType == ServerType.TSERVER ? replicas.prevOldReplicas : currNumTservers;
          tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor =
              serverType == ServerType.TSERVER ? newNumMasters : currNumMasters;

          // partition
          masterPartition =
              serverType == ServerType.TSERVER ? newNumMasters : oldPlacementAZ.replicationFactor;
          tserverPartition =
              serverType == ServerType.TSERVER ? replicas.prevOldReplicas : currNumTservers;

          KubernetesCommandExecutor fullMoveSubTask =
              createFullMoveSubTask(
                  isReadOnlyCluster,
                  provider.getUuid(),
                  azCode,
                  universeName,
                  universeOverrides,
                  azOverrides,
                  serverType == ServerType.TSERVER ? masterAddresses : "",
                  primaryCluster.userIntent.ybSoftwareVersion,
                  config,
                  tempPI,
                  masterPartition,
                  tserverPartition,
                  serverType,
                  enableYbc,
                  ybcSoftwareVersion,
                  masterOldDiskSize /* oldMasterDiskSize */,
                  tsOldDiskSize /* oldTserverDiskSize */,
                  useNewMasterDeviceInfo,
                  useNewTserverDeviceInfo,
                  moveParams);
          if (!fullMoveSubTaskGroupsMap.containsKey(index)) {
            fullMoveSubTaskGroupsMap.put(
                index,
                createSubTaskGroup(
                        KubernetesCommandExecutor.CommandType.HELM_UPGRADE.getSubTaskGroupName())
                    .setSubTaskGroupType(SubTaskGroupType.Provisioning));
          }
          fullMoveSubTaskGroupsMap.get(index).addSubTask(fullMoveSubTask);

          // Add wait for pods before pods delete
          int podsToWaitFor =
              (serverType == ServerType.TSERVER ? newNumMasters : currNumTservers)
                  + replicas.newReplicas
                  + replicas.prevOldReplicas;
          if (!waitForPodsPreDeleteSubTaskGroupsMap.containsKey(index)) {
            waitForPodsPreDeleteSubTaskGroupsMap.put(
                index,
                createSubTaskGroup(
                        KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS.getSubTaskGroupName())
                    .setSubTaskGroupType(SubTaskGroupType.Provisioning));
          }
          waitForPodsPreDeleteSubTaskGroupsMap
              .get(index)
              .addSubTask(
                  createKubernetesCheckPodNumTask(
                      universeName,
                      KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS,
                      azCode,
                      config,
                      podsToWaitFor,
                      isReadOnlyCluster));
        }

        if (CollectionUtils.isNotEmpty(replicas.oldNodeList)) {
          // pod delete params
          PlacementInfo tempPI = new PlacementInfo();
          PlacementInfoUtil.addPlacementZone(azUUID, tempPI);
          tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ =
              serverType == ServerType.TSERVER ? replicas.oldReplicas : currNumTservers;
          tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor =
              serverType == ServerType.TSERVER ? newNumMasters : replicas.oldReplicas;
          // partition
          masterPartition = serverType == ServerType.TSERVER ? newNumMasters : replicas.oldReplicas;
          tserverPartition =
              serverType == ServerType.TSERVER ? replicas.oldReplicas : currNumTservers;

          KubernetesCommandExecutor podDeleteSubtask =
              createKubernetesExecutorTaskForServerType(
                  universeName,
                  CommandType.HELM_UPGRADE,
                  tempPI,
                  azCode,
                  serverType == ServerType.TSERVER ? masterAddresses : "",
                  primaryCluster.userIntent.ybSoftwareVersion,
                  serverType,
                  config,
                  masterPartition,
                  tserverPartition,
                  universeOverrides,
                  azOverrides,
                  isReadOnlyCluster,
                  enableYbc,
                  false /* usePreviousGflagsChecksum */,
                  false /* namespacedServiceReleaseOwner */,
                  null /* ysqlMajorVersionUpgradeState */,
                  moveParams,
                  masterOldDiskSize /* oldMasterDiskSize */,
                  tsOldDiskSize /* oldTserverDiskSize */,
                  useNewMasterDeviceInfo,
                  useNewTserverDeviceInfo);

          if (!podsDeleteSubTaskGroupsMap.containsKey(index)) {
            podsDeleteSubTaskGroupsMap.put(
                index,
                createSubTaskGroup(
                        KubernetesCommandExecutor.CommandType.HELM_UPGRADE.getSubTaskGroupName())
                    .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers));
          }
          podsDeleteSubTaskGroupsMap.get(index).addSubTask(podDeleteSubtask);

          // Add wait for pods after pods delete
          int podsToWaitFor =
              (serverType == ServerType.TSERVER ? newNumMasters : currNumTservers)
                  + replicas.newReplicas
                  + replicas.oldReplicas;
          if (!waitForPodsPostDeleteSubTaskGroupsMap.containsKey(index)) {
            waitForPodsPostDeleteSubTaskGroupsMap.put(
                index,
                createSubTaskGroup(
                        KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS.getSubTaskGroupName())
                    .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers));
          }
          waitForPodsPostDeleteSubTaskGroupsMap
              .get(index)
              .addSubTask(
                  createKubernetesCheckPodNumTask(
                      universeName,
                      KubernetesCheckNumPod.CommandType.WAIT_FOR_PODS,
                      azCode,
                      config,
                      podsToWaitFor,
                      isReadOnlyCluster));
        }

        // Add nodes to add
        if (CollectionUtils.isNotEmpty(replicas.newNodeList)) {
          if (!serversToAddPerSubtaskGroupMap.containsKey(index)) {
            serversToAddPerSubtaskGroupMap.put(index, new ArrayList<>());
          }
          serversToAddPerSubtaskGroupMap.get(index).addAll(replicas.newNodeList);
        }
        // Add pods to add
        if (CollectionUtils.isNotEmpty(replicas.newPodNames)) {
          if (!podNamesToAddPerSubtaskGroupMap.containsKey(index)) {
            podNamesToAddPerSubtaskGroupMap.put(index, new ArrayList<>());
          }
          podNamesToAddPerSubtaskGroupMap.get(index).addAll(replicas.newPodNames);
        }

        // Add nodes to remove
        if (CollectionUtils.isNotEmpty(replicas.oldNodeList)) {
          if (!serversToRemovePerSubtaskGroupMap.containsKey(index)) {
            serversToRemovePerSubtaskGroupMap.put(index, new ArrayList<>());
          }
          serversToRemovePerSubtaskGroupMap.get(index).addAll(replicas.oldNodeList);
        }
        // Add pods to remove
        if (CollectionUtils.isNotEmpty(replicas.oldPodNames)) {
          if (!podNamesToRemovePerSubtaskGroupMap.containsKey(index)) {
            podNamesToRemovePerSubtaskGroupMap.put(index, new ArrayList<>());
          }
          podNamesToRemovePerSubtaskGroupMap.get(index).addAll(replicas.oldPodNames);
        }
        index++;
      }

      // Helm upgrade finally to apply new Placement and device info to new STS
      // and make start == end index
      PlacementInfo tempPI = new PlacementInfo();
      PlacementInfoUtil.addPlacementZone(azUUID, tempPI);
      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ =
          serverType == ServerType.TSERVER ? newNumTservers : currNumTservers;
      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor = newNumMasters;
      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).masterStsIndex =
          newPlacementAZ.masterStsIndex;
      tempPI.cloudList.get(0).regionList.get(0).azList.get(0).tsStsIndex =
          serverType == ServerType.TSERVER ? newPlacementAZ.tsStsIndex : oldPlacementAZ.tsStsIndex;
      // For disk params:
      // useNewMasterDeviceInfo: Should be true as this helm upgrade is after moving masters.
      // useNewTserverDeviceInfo: True if TSERVER. For MASTER, true if TSERVER does not go via
      // full move, otherwise false.
      boolean useNewMasterDeviceInfo = true;
      boolean useNewTserverDeviceInfo =
          serverType == ServerType.TSERVER
              ? true
              : CollectionUtils.isEmpty(tsFullMoveAZs) || !tsFullMoveAZs.contains(azUUID);
      helmUpgrades.addSubTask(
          createKubernetesExecutorTaskForServerType(
              universeName,
              CommandType.HELM_UPGRADE,
              tempPI,
              azCode,
              serverType == ServerType.TSERVER ? masterAddresses : "",
              primaryCluster.userIntent.ybSoftwareVersion,
              serverType,
              config,
              newPlacementAZ.replicationFactor /* masterPartition */,
              serverType == ServerType.TSERVER
                  ? newPlacementAZ.numNodesInAZ
                  : currNumTservers /* tserverPartition */,
              universeOverrides,
              azOverrides,
              isReadOnlyCluster,
              enableYbc,
              false /* usePreviousGflagsChecksum */,
              false /* namespacedServiceReleaseOwner */,
              null /* ysqlMajorVersionUpgradeState */,
              null /* moveParams */,
              null /* oldMasterDiskSize */,
              serverType == ServerType.TSERVER ? null : tsOldDiskSize /* oldTserverDiskSize */,
              useNewMasterDeviceInfo,
              useNewTserverDeviceInfo));

      // Garbage collect master volumes for deleted pods
      if (!isReadOnlyCluster && serverType == ServerType.MASTER) {
        pvcDeletes.addSubTask(
            garbageCollectMasterVolumes(
                universeName,
                nodePrefix,
                azCode,
                config,
                newPlacementAZ.replicationFactor,
                isReadOnlyCluster,
                newNamingStyle,
                universe.getUniverseUUID()));
      }
    }

    // Handle subtasks flow and add to running task
    int subTaskGroupsSize =
        Integer.max(
            fullMoveSubTaskGroupsMap.keySet().size(), podsDeleteSubTaskGroupsMap.keySet().size());
    int startIndex =
        Integer.min(
            Collections.min(
                CollectionUtils.isEmpty(fullMoveSubTaskGroupsMap.keySet())
                    ? Set.of(0)
                    : fullMoveSubTaskGroupsMap.keySet()),
            Collections.min(
                CollectionUtils.isEmpty(podsDeleteSubTaskGroupsMap.keySet())
                    ? Set.of(0)
                    : podsDeleteSubTaskGroupsMap.keySet()));
    for (int i = startIndex; i < startIndex + subTaskGroupsSize; i++) {
      log.debug("Handling full move batch: {}", i);
      // Add helm upgrade task
      if (fullMoveSubTaskGroupsMap.containsKey(i)) {
        getRunnableTask().addSubTaskGroup(fullMoveSubTaskGroupsMap.get(i));
      }
      // Add wait for pods after adding pods
      if (waitForPodsPreDeleteSubTaskGroupsMap.containsKey(i)) {
        getRunnableTask().addSubTaskGroup(waitForPodsPreDeleteSubTaskGroupsMap.get(i));
      }

      List<NodeDetails> serversToAdd =
          serversToAddPerSubtaskGroupMap.getOrDefault(i, new ArrayList<>());
      List<NodeDetails> serversToRemove =
          serversToRemovePerSubtaskGroupMap.getOrDefault(i, new ArrayList<>());

      // Common tasks between master and tserver
      Consumer<List<NodeDetails>> commonTasks =
          (toAdd) -> {
            if (CollectionUtils.isNotEmpty(toAdd)) {
              PlacementInfo activeZones = new PlacementInfo();
              for (UUID currAZs : curPlacement.configs.keySet()) {
                PlacementInfoUtil.addPlacementZone(currAZs, activeZones);
              }
              createSingleKubernetesExecutorTask(
                  universeName,
                  KubernetesCommandExecutor.CommandType.POD_INFO,
                  activeZones,
                  isReadOnlyCluster);
              createTransferXClusterCertsCopyTasks(
                  toAdd, getUniverse(), SubTaskGroupType.ConfigureUniverse);
              createWaitForServersTasks(toAdd, serverType)
                  .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
              createSwamperTargetUpdateTask(false /* removeFile */);
            }
          };

      if (serverType == ServerType.MASTER) {
        Set<NodeDetails> mastersToAdd = filterMastersToAdd(new HashSet<>(serversToAdd), universe);
        commonTasks.accept(new ArrayList<>(mastersToAdd));
        // For masters, add change config task
        if (CollectionUtils.isNotEmpty(serversToAdd)
            || CollectionUtils.isNotEmpty(serversToRemove)) {
          createMoveMasterTasks(serversToAdd, serversToRemove);
        }
      } else {
        // Tserver case
        commonTasks.accept(serversToAdd);
        // Install YBC on all newly added pods
        if (universe.isYbcEnabled() && CollectionUtils.isNotEmpty(serversToAdd)) {
          if (!universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()) {
            installYbcOnThePods(
                new HashSet<>(serversToAdd),
                isReadOnlyCluster,
                ybcManager.getStableYbcVersion(),
                isReadOnlyCluster
                    ? universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.ybcFlags
                    : universe.getUniverseDetails().getPrimaryCluster().userIntent.ybcFlags,
                newPlacement);
          } else {
            log.debug("Skipping configure YBC as 'useYBDBInbuiltYbc' is enabled");
          }
          createWaitForYbcServerTask(serversToAdd);
        }
        if (CollectionUtils.isNotEmpty(serversToRemove)) {
          // Update the blacklist servers on master leader.
          createPlacementInfoTask(serversToRemove, taskParams().clusters)
              .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
          // If the tservers have been removed, move the data.
          createWaitForDataMoveTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
        }
        if (CollectionUtils.isNotEmpty(serversToAdd)
            && confGetter.getConfForScope(universe, UniverseConfKeys.waitForLbForAddedNodes)) {
          // If tservers have been added, we wait for the load to balance.
          createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
        }
      }

      // We are concatenating streams so passing as any server type is fine
      if (CollectionUtils.isNotEmpty(serversToRemove)) {
        createCheckNodeSafeToDeleteTasks(
            universe.getUniverseUUID(),
            new HashSet<>(serversToRemove),
            new HashSet<>() /* tserversToRemove */);
      }
      // Add pod delete tasks
      if (podsDeleteSubTaskGroupsMap.containsKey(i)) {
        getRunnableTask().addSubTaskGroup(podsDeleteSubTaskGroupsMap.get(i));
      }
      // Add wait for pods after deleting pods
      if (waitForPodsPostDeleteSubTaskGroupsMap.containsKey(i)) {
        getRunnableTask().addSubTaskGroup(waitForPodsPostDeleteSubTaskGroupsMap.get(i));
      }
      // Save the current batch index to taskParams for retryability
      getRunnableTask()
          .addSubTaskGroup(createPersistFullMoveBatchInTaskParams(i, clusterUUID, serverType));
    }
    // Add final helm upgrades( to start using new values with new STS index )
    if (helmUpgrades.getSubTaskCount() > 0) {
      getRunnableTask().addSubTaskGroup(helmUpgrades);
    }
    // Delete non required PVCs
    if (pvcDeletes.getSubTaskCount() > 0) {
      getRunnableTask().addSubTaskGroup(pvcDeletes);
    }
  }

  /**
   * Check if volume size changed for any AZs. We will skip AZs which are going through full move(
   * i.e. deviceInfo other than volume size changed )
   *
   * @param curPlacement
   * @param newPlacement
   * @param newIntent
   * @param curIntent
   * @param nodeDetailsSet
   * @return Map of az UUID and corresponding pair of boolean indicating whether (tserver, master)
   *     volume size changed
   */
  protected Map<UUID, Pair<Boolean, Boolean>> canResizeDisk(
      KubernetesPlacement curPlacement,
      KubernetesPlacement newPlacement,
      UserIntent newIntent,
      UserIntent curIntent,
      Set<NodeDetails> nodeDetailsSet) {
    Map<UUID, Pair<Boolean, Boolean>> azServerTypeVolumeSizeChangedMap = new HashMap<>();
    Set<UUID> newAzUUIDs = newPlacement.placementInfo.getAllAZUUIDs();
    Iterator<PlacementAZ> azIter = curPlacement.placementInfo.azStream().iterator();
    while (azIter.hasNext()) {
      boolean tserverDiskSizeChanged = false, masterDiskSizeChanged = false;
      DeviceInfo oldDeviceInfo, newDeviceInfo;
      PlacementAZ az = azIter.next();
      if (!newAzUUIDs.contains(az.uuid)) {
        continue;
      }
      oldDeviceInfo = curIntent.getDeviceInfoForAz(az.uuid, false /* isDedicatedMaster */);
      newDeviceInfo = newIntent.getDeviceInfoForAz(az.uuid, false /* isDedicatedMaster */);
      if (oldDeviceInfo.onlyVolumeSizeChanged(newDeviceInfo)) {
        tserverDiskSizeChanged = true;
        log.info(
            "Creating task for tserver disk size change from {} to {} for AZ {}",
            oldDeviceInfo.volumeSize,
            newDeviceInfo.volumeSize,
            az.name);
      }
      oldDeviceInfo = curIntent.getDeviceInfoForAz(az.uuid, true /* isDedicatedMaster */);
      newDeviceInfo = newIntent.getDeviceInfoForAz(az.uuid, true /* isDedicatedMaster */);
      if (oldDeviceInfo.onlyVolumeSizeChanged(newDeviceInfo)) {
        masterDiskSizeChanged = true;
        log.info(
            "Creating task for master disk size change from {} to {} for AZ {}",
            oldDeviceInfo.volumeSize,
            newDeviceInfo.volumeSize,
            az.name);
      }
      if (tserverDiskSizeChanged || masterDiskSizeChanged) {
        azServerTypeVolumeSizeChangedMap.put(
            az.uuid, new Pair<>(tserverDiskSizeChanged, masterDiskSizeChanged));
      }
    }
    return azServerTypeVolumeSizeChangedMap;
  }

  // Save full move batch index for correct retryability
  private SubTaskGroup createPersistFullMoveBatchInTaskParams(
      int index, UUID clusterUUID, ServerType serverType) {
    Consumer<TaskInfo> taskInfoConsumer =
        tf -> {
          if (serverType == ServerType.TSERVER) {
            taskParams().getClusterByUuid(clusterUUID).setCurrentTsFullMoveBatchIndex(index);
          } else {
            taskParams().getClusterByUuid(clusterUUID).setCurrentMasterFullMoveBatchIndex(index);
          }
          tf.setTaskParams(Json.toJson(taskParams()));
        };
    return getUpdateParentTaskParamsTask(taskInfoConsumer);
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
      Map<UUID, Pair<Boolean, Boolean>> azToDiskSizeChangeMap,
      boolean usePreviousGflagsChecksum,
      Set<UUID> skipMasterAZs,
      Set<UUID> skipTserverAZs) {
    // For non-restart way of master addresses change, we get the previous STS
    // gflags checksum value. For disk resize case, if STS is deleted and task fails,
    // need to have the checksum value available.
    if (usePreviousGflagsChecksum) {
      persistGflagsChecksumInTaskParams(universeName);
      // persist cert checksum in task params for non-restart pods upgrade.
      persistCertChecksumInTaskParams(universeName);
    }
    if (CollectionUtils.isNotEmpty(skipTserverAZs) || CollectionUtils.isNotEmpty(skipMasterAZs)) {
      persistOriginalDiskSizeInTaskParams(clusterUUID);
    }
    BiFunction<Map<UUID, Pair<Boolean, Boolean>>, Predicate<Pair<Boolean, Boolean>>, Set<UUID>>
        diskSizeChangeAZs =
            (azDSMap, testCond) ->
                azDSMap.entrySet().stream()
                    .filter(e -> testCond.test(e.getValue()))
                    .map(Map.Entry::getKey)
                    .collect(Collectors.toSet());
    Set<UUID> masterChangeAZs =
        diskSizeChangeAZs.apply(azToDiskSizeChangeMap, p -> p.getSecond() == true);
    Set<UUID> tserverChangeAZs =
        diskSizeChangeAZs.apply(azToDiskSizeChangeMap, p -> p.getFirst() == true);

    if (CollectionUtils.isNotEmpty(masterChangeAZs) && !isReadOnlyCluster) {
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
          ServerType.MASTER,
          skipMasterAZs,
          skipTserverAZs);
    }
    if (CollectionUtils.isNotEmpty(tserverChangeAZs)) {
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
          ServerType.TSERVER,
          skipMasterAZs,
          skipTserverAZs);
    }
    // persist the disk size changes to the universe
    createPersistResizeNodeTask(
        userIntent, clusterUUID, true /* onlyPersistDeviceInfo */, skipMasterAZs, skipTserverAZs);
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
      ServerType serverType,
      Set<UUID> masterFullMoveAZs,
      Set<UUID> tserverFullMoveAZs) {
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
    String softwareVersion = userIntent.ybSoftwareVersion;
    Provider provider = Util.getSingleProvider(newCluster);

    for (Entry<UUID, Map<String, String>> entry : placement.configs.entrySet()) {
      UUID azUUID = entry.getKey();
      Set<UUID> skipAZs = serverType == ServerType.TSERVER ? tserverFullMoveAZs : masterFullMoveAZs;
      if (CollectionUtils.isNotEmpty(skipAZs) && skipAZs.contains(azUUID)) {
        continue;
      }
      String newDiskSizeGi =
          String.format(
              "%dGi",
              userIntent.getDeviceInfoForAz(azUUID, serverType == ServerType.MASTER).volumeSize);

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
                provider.getUuid(),
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
                false /* useNewMasterDeviceInfo */,
                false /* useNewTserverDeviceInfo */,
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
                false /* useNewMasterDeviceInfo */,
                false /* useNewTserverDeviceInfo */,
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
      boolean useNewMasterDeviceInfo =
          serverType == ServerType.TSERVER
              ? CollectionUtils.isEmpty(masterFullMoveAZs) || !masterFullMoveAZs.contains(azUUID)
              : true;
      boolean useNewTserverDeviceInfo = serverType == ServerType.TSERVER ? true : false;
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
              useNewMasterDeviceInfo,
              useNewTserverDeviceInfo,
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
              provider.getUuid(),
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

  protected void persistOriginalDiskSizeInTaskParams(UUID clusterUUID) {
    Cluster cluster = getUniverse().getCluster(clusterUUID);
    if (MapUtils.isNotEmpty(taskParams().getClusterByUuid(cluster.uuid).getOriginalDiskSize())) {
      return;
    }

    Map<UUID, Map<ServerType, Integer>> originalDiskSizeMap = new HashMap<>();
    cluster.placementInfo.getAllAZUUIDs().stream()
        .forEach(
            azUUID -> {
              Map<ServerType, Integer> diskSizes = new HashMap<>();
              diskSizes.put(
                  ServerType.MASTER,
                  cluster.userIntent.getDeviceInfoForAz(azUUID, true /* idDedicatedMaster */)
                      .volumeSize);
              diskSizes.put(
                  ServerType.TSERVER,
                  cluster.userIntent.getDeviceInfoForAz(azUUID, false /* idDedicatedMaster */)
                      .volumeSize);
              originalDiskSizeMap.put(azUUID, diskSizes);
            });
    taskParams().getClusterByUuid(cluster.uuid).setOriginalDiskSize(originalDiskSizeMap);
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

      // Not allowing re-run if device info change was attempted
      Map<ServerType, Boolean> deviceInfoChanged =
          getServerTypeDeviceInfoChangedMap(newCluster, currCluster);
      if (deviceInfoChanged.get(ServerType.TSERVER) || deviceInfoChanged.get(ServerType.MASTER)) {
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

  private static Map<ServerType, Boolean> getServerTypeDeviceInfoChangedMap(
      Cluster newCluster, Cluster currCluster) {
    boolean masterVolumeChanged = false, tserverVolumeChanged = false;
    KubernetesPlacement newPlacement =
        new KubernetesPlacement(
            newCluster.placementInfo, newCluster.clusterType == ClusterType.ASYNC);
    for (Entry<UUID, Map<String, String>> entry : newPlacement.configs.entrySet()) {
      DeviceInfo taskDeviceInfo =
          newCluster.userIntent.getDeviceInfoForAz(entry.getKey(), false /* isDedicatedMaster */);
      DeviceInfo existingDeviceInfo =
          currCluster.userIntent.getDeviceInfoForAz(entry.getKey(), false /* isDedicatedMaster */);
      if (taskDeviceInfo != null
          && existingDeviceInfo != null
          && !taskDeviceInfo.equals(existingDeviceInfo)) {
        log.debug("Volume config changed for AZ {} for tserver", entry.getKey());
        tserverVolumeChanged = true;
      }
      DeviceInfo taskMasterDeviceInfo =
          newCluster.userIntent.getDeviceInfoForAz(entry.getKey(), true /* isDedicatedMaster */);
      DeviceInfo existingMasterDeviceInfo =
          currCluster.userIntent.getDeviceInfoForAz(entry.getKey(), true /* isDedicatedMaster */);
      if (taskMasterDeviceInfo != null
          && existingMasterDeviceInfo != null
          && !taskMasterDeviceInfo.equals(existingMasterDeviceInfo)) {
        log.debug("Volume config changed for AZ {} for master", entry.getKey());
        masterVolumeChanged = true;
      }
    }
    Map<ServerType, Boolean> deviceInfoChanged = new HashMap<>();
    deviceInfoChanged.put(ServerType.TSERVER, tserverVolumeChanged);
    deviceInfoChanged.put(ServerType.MASTER, masterVolumeChanged);
    return deviceInfoChanged;
  }
}
