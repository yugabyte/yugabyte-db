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
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class DestroyKubernetesUniverse extends DestroyUniverse {

  private final XClusterUniverseService xClusterUniverseService;
  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  public DestroyKubernetesUniverse(
      BaseTaskDependencies baseTaskDependencies,
      XClusterUniverseService xClusterUniverseService,
      OperatorStatusUpdaterFactory statusUpdaterFactory) {
    super(baseTaskDependencies, xClusterUniverseService);
    this.xClusterUniverseService = xClusterUniverseService;
    this.kubernetesStatus = statusUpdaterFactory.create();
  }

  @Override
  public void run() {
    try {

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = null;
      if (params().isForceDelete) {
        universe = forceLockUniverseForUpdate(-1);
      } else {
        universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */);
      }
      kubernetesStatus.createYBUniverseEventStatus(
          universe, params().getKubernetesResourceDetails(), getName(), getUserTaskUUID());
      // Delete xCluster configs involving this universe and put the locked universes to
      // lockedUniversesUuidList.
      createDeleteXClusterConfigSubtasksAndLockOtherUniverses();

      // Promote auto flags on all universes which were blocked due to the xCluster config.
      // No need to pass excludeXClusterConfigSet as they are updated with status DeletedUniverse.
      createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
          lockedXClusterUniversesUuidSet,
          Stream.of(universe.getUniverseUUID()).collect(Collectors.toSet()),
          xClusterUniverseService,
          new HashSet<>() /* excludeXClusterConfigSet */,
          params().isForceDelete);

      if (params().isDeleteBackups) {
        List<Backup> backupList =
            Backup.fetchBackupToDeleteByUniverseUUID(
                params().customerUUID, universe.getUniverseUUID());
        createDeleteBackupYbTasks(backupList, params().customerUUID)
            .setSubTaskGroupType(SubTaskGroupType.DeletingBackup);
      }

      preTaskActions();

      Map<String, String> universeConfig = universe.getConfig();
      // True for all the new and v2 to v3 migrated universes
      // i.e. everything which is using 2.1.8+.
      boolean runHelmDelete = universeConfig.containsKey(Universe.HELM2_LEGACY);

      // Cleanup the kms_history table
      createDestroyEncryptionAtRestTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      // Try to unify this with the edit remove pods/deployments flow. Currently delete is
      // tied down to a different base class which makes params porting not straight-forward.
      SubTaskGroup helmDeletes =
          createSubTaskGroup(
              KubernetesCommandExecutor.CommandType.HELM_DELETE.getSubTaskGroupName());
      helmDeletes.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      SubTaskGroup volumeDeletes =
          createSubTaskGroup(
              KubernetesCommandExecutor.CommandType.VOLUME_DELETE.getSubTaskGroupName());
      volumeDeletes.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      SubTaskGroup namespaceDeletes =
          createSubTaskGroup(
              KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE.getSubTaskGroupName());
      namespaceDeletes.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      for (UniverseDefinitionTaskParams.Cluster cluster : universe.getUniverseDetails().clusters) {
        UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;
        UUID providerUUID = UUID.fromString(userIntent.provider);

        PlacementInfo pi = cluster.placementInfo;

        Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));

        Map<UUID, Map<String, String>> azToConfig = KubernetesUtil.getConfigPerAZ(pi);
        boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

        for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
          UUID azUUID = entry.getKey();
          String azName = isMultiAz ? AvailabilityZone.getOrBadRequest(azUUID).getCode() : null;

          Map<String, String> config = entry.getValue();

          String namespace = config.get("KUBENAMESPACE");

          if (runHelmDelete || namespace != null) {
            // Delete the helm deployments.
            helmDeletes.addSubTask(
                createDestroyKubernetesTask(
                    universe.getUniverseDetails().nodePrefix,
                    universe.getName(),
                    universe.getUniverseDetails().useNewHelmNamingStyle,
                    azName,
                    config,
                    KubernetesCommandExecutor.CommandType.HELM_DELETE,
                    providerUUID,
                    cluster.clusterType == ClusterType.ASYNC));
          }

          // Delete the PVCs created for this AZ.
          volumeDeletes.addSubTask(
              createDestroyKubernetesTask(
                  universe.getUniverseDetails().nodePrefix,
                  universe.getName(),
                  universe.getUniverseDetails().useNewHelmNamingStyle,
                  azName,
                  config,
                  KubernetesCommandExecutor.CommandType.VOLUME_DELETE,
                  providerUUID,
                  cluster.clusterType == ClusterType.ASYNC));

          // TODO(bhavin192): delete the pull secret as well? As of now,
          // we depend on the fact that, deleting the namespace will
          // delete the pull secret. That won't be the case with
          // providers which have KUBENAMESPACE paramter in the AZ
          // config. How to find the pull secret name? Should we delete
          // it when we have multiple releases in one namespace?. It is
          // possible that same provider is creating multiple releases
          // in one namespace. Tracked here:
          // https://github.com/yugabyte/yugabyte-db/issues/7012

          // Delete the namespaces of the deployments only if those were
          // created by us.
          if (namespace == null) {
            namespaceDeletes.addSubTask(
                createDestroyKubernetesTask(
                    universe.getUniverseDetails().nodePrefix,
                    universe.getName(),
                    universe.getUniverseDetails().useNewHelmNamingStyle,
                    azName,
                    config,
                    KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE,
                    providerUUID,
                    cluster.clusterType == ClusterType.ASYNC));
          }
        }
      }

      getRunnableTask().addSubTaskGroup(helmDeletes);
      getRunnableTask().addSubTaskGroup(volumeDeletes);
      getRunnableTask().addSubTaskGroup(namespaceDeletes);

      // Create tasks to remove the universe entry from the Universe table.
      createRemoveUniverseEntryTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(true /* removeFile */);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          params().getKubernetesResourceDetails(),
          getName(),
          getUserTaskUUID(),
          null);
    } catch (Throwable t) {
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(), params().getKubernetesResourceDetails(), getName(), getUserTaskUUID(), t);
      // If for any reason destroy fails we would just unlock the universe for update
      try {
        unlockUniverseForUpdate();
      } catch (Throwable t1) {
        // Ignore the error
      }
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockXClusterUniverses(lockedXClusterUniversesUuidSet, params().isForceDelete);
    }
    log.info("Finished {} task.", getName());
  }

  protected KubernetesCommandExecutor createDestroyKubernetesTask(
      String nodePrefix,
      String universeName,
      boolean newNamingStyle,
      String az,
      Map<String, String> config,
      KubernetesCommandExecutor.CommandType commandType,
      UUID providerUUID,
      boolean isReadOnlyCluster) {
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.commandType = commandType;
    params.providerUUID = providerUUID;
    params.isReadOnlyCluster = isReadOnlyCluster;
    params.universeName = universeName;
    params.azCode = az;
    params.helmReleaseName =
        KubernetesUtil.getHelmReleaseName(
            nodePrefix, universeName, az, isReadOnlyCluster, newNamingStyle);
    if (config != null) {
      params.config = config;
      // This assumes that the config is az config. It is true in this
      // particular case, all callers just pass az config.
      // params.namespace remains null if config is not passed.
      params.namespace =
          KubernetesUtil.getKubernetesNamespace(
              nodePrefix, az, config, newNamingStyle, isReadOnlyCluster);
    }
    params.setUniverseUUID(params().getUniverseUUID());
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    return task;
  }
}
