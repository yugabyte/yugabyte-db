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
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.operator.KubernetesOperatorStatusUpdater;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class EditKubernetesUniverse extends KubernetesTaskBase {

  static final int DEFAULT_WAIT_TIME_MS = 10000;
  private final KubernetesOperatorStatusUpdater kubernetesStatus;
  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected EditKubernetesUniverse(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesOperatorStatusUpdater kubernetesStatus,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies);
    this.kubernetesStatus = kubernetesStatus;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  @Override
  public void run() {
    Throwable th = null;
    try {
      checkUniverseVersion();
      // Verify the task params.
      verifyParams(UniverseOpType.EDIT);
      // TODO: Would it make sense to have a precheck k8s task that does
      // some precheck operations to verify kubeconfig, svcaccount, connectivity to universe here ?
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      kubernetesStatus.createYBUniverseEventStatus(
          universe, taskParams().getKubernetesResourceDetails(), getName(), getUserTaskUUID());
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

      PlacementInfo primaryPI = primaryCluster.placementInfo;
      int numMasters = primaryCluster.userIntent.replicationFactor;
      PlacementInfoUtil.selectNumMastersAZ(primaryPI, numMasters);
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
              false /* restartAllPods */);

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
      th = t;
      throw t;
    } finally {
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          getName(),
          getUserTaskUUID(),
          th);
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
    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    }

    KubernetesPlacement newPlacement = new KubernetesPlacement(newPI, isReadOnlyCluster),
        curPlacement = new KubernetesPlacement(curPI, isReadOnlyCluster);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(newIntent.provider));
    boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
    boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

    // Update disk size if there is a change
    boolean diskSizeChanged =
        !curIntent.deviceInfo.volumeSize.equals(newIntent.deviceInfo.volumeSize);
    if (diskSizeChanged) {
      log.info(
          "Creating task for disk size change from {} to {}",
          curIntent.deviceInfo.volumeSize,
          newIntent.deviceInfo.volumeSize);
      createResizeDiskTask(
          universe.getName(),
          curPlacement,
          masterAddresses,
          newIntent,
          isReadOnlyCluster,
          newNamingStyle,
          universe.isYbcEnabled(),
          universe.getUniverseDetails().getYbcSoftwareVersion());
    }

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
      if (curIntent.masterK8SNodeResourceSpec != null) {
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
      restartAllPods = true;
      startNewPods(
          universe.getName(),
          newMasters,
          ServerType.MASTER,
          activeZones,
          isReadOnlyCluster,
          /*masterAddresses*/ "",
          newPlacement,
          curPlacement);

      // Update master addresses to the latest required ones,
      // We use the original unfiltered mastersToAdd which is determined from pi.
      createMoveMasterTasks(new ArrayList<>(mastersToAdd), new ArrayList<>(mastersToRemove));
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
          universe.isYbcEnabled());

      if (universe.isYbcEnabled()) {
        installYbcOnThePods(
            universe.getName(),
            tserversToAdd,
            isReadOnlyCluster,
            universe.getUniverseDetails().getYbcSoftwareVersion());
        createWaitForYbcServerTask(tserversToAdd);
      }
    }

    // Update the blacklist servers on master leader.
    createPlacementInfoTask(tserversToRemove)
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

    String universeOverrides = primaryCluster.userIntent.universeOverrides;
    Map<String, String> azOverrides = primaryCluster.userIntent.azOverrides;
    if (azOverrides == null) {
      azOverrides = new HashMap<String, String>();
    }
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
          DEFAULT_WAIT_TIME_MS,
          universeOverrides,
          azOverrides,
          true,
          true,
          newNamingStyle,
          isReadOnlyCluster);
    }
    if (instanceTypeChanged || restartAllPods) {
      upgradePodsTask(
          universe.getName(),
          newPlacement,
          masterAddresses,
          curPlacement,
          ServerType.TSERVER,
          newIntent.ybSoftwareVersion,
          DEFAULT_WAIT_TIME_MS,
          universeOverrides,
          azOverrides,
          false,
          true,
          newNamingStyle,
          isReadOnlyCluster,
          KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
          universe.isYbcEnabled(),
          universe.getUniverseDetails().getYbcSoftwareVersion());
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
          universe.isYbcEnabled());
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
          universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.JWT_JWKS);
    }

    if (!mastersToAdd.isEmpty()) {
      // Update the master addresses on the target universes whose source universe belongs to
      // this task.
      createXClusterConfigUpdateMasterAddressesTask();
    }

    return !mastersToAdd.isEmpty();
  }

  private void validateEditParams(Cluster newCluster, Cluster curCluster) {
    // TODO we should look for y(c)sql auth, gflags changes and so on.
    // Move this logic to UniverseDefinitionTaskBase.
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

  public void startNewPods(
      String universeName,
      Set<NodeDetails> podsToAdd,
      ServerType serverType,
      PlacementInfo activeZones,
      boolean isReadOnlyCluster,
      String masterAddresses,
      KubernetesPlacement newPlacement,
      KubernetesPlacement currPlacement) {
    startNewPods(
        universeName,
        podsToAdd,
        serverType,
        activeZones,
        isReadOnlyCluster,
        masterAddresses,
        newPlacement,
        currPlacement,
        false);
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
      boolean enableYbc) {
    createPodsTask(
        universeName,
        newPlacement,
        masterAddresses,
        currPlacement,
        serverType,
        activeZones,
        isReadOnlyCluster,
        enableYbc);

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
      String ybcSoftwareVersion) {

    // The method to expand disk size is:
    // 1. Delete statefulset without deleting the pods
    // 2. Patch PVC to new disk size
    // 3. Run helm upgrade so that new StatefulSet is created with updated disk size.
    // The newly created statefulSet also claims the already running pods.
    String newDiskSizeGi = String.format("%dGi", userIntent.deviceInfo.volumeSize);
    String softwareVersion = userIntent.ybSoftwareVersion;
    UUID providerUUID = UUID.fromString(userIntent.provider);
    Provider provider = Provider.getOrBadRequest(providerUUID);
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(provider);
    for (Entry<UUID, Map<String, String>> entry : placement.configs.entrySet()) {

      UUID azUUID = entry.getKey();
      String azName =
          PlacementInfoUtil.isMultiAZ(provider)
              ? AvailabilityZone.getOrBadRequest(azUUID).getCode()
              : null;

      String namespace =
          KubernetesUtil.getKubernetesNamespace(
              taskParams().nodePrefix, azName, config, newNamingStyle, isReadOnlyCluster);

      String helmReleaseName =
          KubernetesUtil.getHelmReleaseName(
              taskParams().nodePrefix, universeName, azName, isReadOnlyCluster, newNamingStyle);

      boolean needsExpandPVCInZone =
          KubernetesUtil.needsExpandPVC(
              namespace,
              helmReleaseName,
              "yb-tserver",
              newNamingStyle,
              newDiskSizeGi,
              config,
              kubernetesManagerFactory);
      if (!needsExpandPVCInZone) {
        log.info("PVC size is unchanged, will not schedule resize tasks");
        continue;
      }

      Map<String, String> azConfig = entry.getValue();
      PlacementInfo azPI = new PlacementInfo();
      int rf = placement.masters.getOrDefault(azUUID, 0);
      int numNodesInAZ = placement.tservers.getOrDefault(azUUID, 0);
      PlacementInfoUtil.addPlacementZone(azUUID, azPI, rf, numNodesInAZ, true);
      // Validate that the StorageClass has allowVolumeExpansion=true
      createTaskToValidateExpansion(
          universeName, azConfig, azName, isReadOnlyCluster, newNamingStyle, providerUUID);
      // create the three tasks to update volume size
      createSingleKubernetesExecutorTaskForServerType(
          universeName,
          KubernetesCommandExecutor.CommandType.STS_DELETE,
          azPI,
          azName,
          masterAddresses,
          softwareVersion,
          ServerType.TSERVER,
          azConfig,
          0,
          0,
          null,
          null,
          isReadOnlyCluster,
          null,
          newDiskSizeGi,
          false,
          enableYbc,
          ybcSoftwareVersion);
      createSingleKubernetesExecutorTaskForServerType(
          universeName,
          KubernetesCommandExecutor.CommandType.PVC_EXPAND_SIZE,
          azPI,
          azName,
          masterAddresses,
          softwareVersion,
          ServerType.TSERVER,
          azConfig,
          0,
          0,
          null,
          null,
          isReadOnlyCluster,
          null,
          newDiskSizeGi,
          true,
          enableYbc,
          ybcSoftwareVersion);
      createSingleKubernetesExecutorTaskForServerType(
          universeName,
          KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
          azPI,
          azName,
          masterAddresses,
          softwareVersion,
          ServerType.TSERVER,
          azConfig,
          0,
          0,
          null,
          null,
          isReadOnlyCluster,
          null,
          newDiskSizeGi,
          false,
          enableYbc,
          ybcSoftwareVersion);
      createPostExpansionValidateTask(
          universeName,
          azConfig,
          azName,
          isReadOnlyCluster,
          newNamingStyle,
          providerUUID,
          newDiskSizeGi);
    }
  }

  private void createTaskToValidateExpansion(
      String universeName,
      Map<String, String> config,
      String azName,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      UUID providerUUID) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup(KubernetesCheckVolumeExpansion.getSubTaskGroupName());
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
    KubernetesCheckVolumeExpansion task = createTask(KubernetesCheckVolumeExpansion.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createPostExpansionValidateTask(
      String universeName,
      Map<String, String> config,
      String azName,
      boolean isReadOnlyCluster,
      boolean newNamingStyle,
      UUID providerUUID,
      String newDiskSizeGi) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor()
            .createSubTaskGroup(KubernetesPostExpansionCheckVolume.getSubTaskGroupName());
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
    KubernetesPostExpansionCheckVolume task = createTask(KubernetesPostExpansionCheckVolume.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }
}
