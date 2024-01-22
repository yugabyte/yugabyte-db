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
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteCertificate;
import com.yugabyte.yw.commissioner.tasks.subtasks.RemoveUniverseEntry;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.UniverseInProgressException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DestroyUniverse extends UniverseTaskBase {

  private final XClusterUniverseService xClusterUniverseService;
  private final SupportBundleUtil supportBundleUtil;

  @Inject
  public DestroyUniverse(
      BaseTaskDependencies baseTaskDependencies,
      XClusterUniverseService xClusterUniverseService,
      SupportBundleUtil supportBundleUtil) {
    super(baseTaskDependencies);
    this.xClusterUniverseService = xClusterUniverseService;
    this.supportBundleUtil = supportBundleUtil;
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
    public Boolean isForceDelete;
    public Boolean isDeleteBackups;
    public Boolean isDeleteAssociatedCerts;

    @ApiModelProperty(hidden = true)
    @Getter
    @Setter
    KubernetesResourceDetails kubernetesResourceDetails;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  protected void validateUniverseState(Universe universe) {
    try {
      super.validateUniverseState(universe);
    } catch (UniverseInProgressException e) {
      if (!params().isForceDelete) {
        throw e;
      }
    }
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe;
      if (params().isForceDelete) {
        universe = forceLockUniverseForUpdate(-1);
      } else {
        universe = lockUniverseForUpdate(-1);
      }

      // Delete xCluster configs involving this universe and put the locked universes to
      // lockedUniversesUuidList.
      createDeleteXClusterConfigSubtasksAndLockOtherUniverses();

      Set<Universe> xClusterConnectedUniverseSet = new HashSet<>();
      xClusterConnectedUniverseSet.add(universe);
      lockedXClusterUniversesUuidSet.forEach(
          uuid -> {
            xClusterConnectedUniverseSet.add(Universe.getOrBadRequest(uuid));
          });

      if (xClusterConnectedUniverseSet.stream()
          .anyMatch(
              univ ->
                  CommonUtils.isReleaseBefore(
                      Util.YBDB_ROLLBACK_DB_VERSION,
                      univ.getUniverseDetails()
                          .getPrimaryCluster()
                          .userIntent
                          .ybSoftwareVersion))) {
        // Promote auto flags on all universes which were blocked due to the xCluster config.
        // No need to send excludeXClusterConfigSet as they are updated with status DeletedUniverse.
        createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
            lockedXClusterUniversesUuidSet,
            Stream.of(universe.getUniverseUUID()).collect(Collectors.toSet()),
            xClusterUniverseService,
            new HashSet<>() /* excludeXClusterConfigSet */,
            params().isForceDelete);
      }

      if (params().isDeleteBackups) {
        List<Backup> backupList =
            Backup.fetchBackupToDeleteByUniverseUUID(
                params().customerUUID, universe.getUniverseUUID());
        createDeleteBackupYbTasks(backupList, params().customerUUID)
            .setSubTaskGroupType(SubTaskGroupType.DeletingBackup);
      }

      // cleanup the supportBundles if any
      deleteSupportBundle(universe.getUniverseUUID());

      preTaskActions();

      // Cleanup the kms_history table
      createDestroyEncryptionAtRestTask()
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

      if (!universe.getUniverseDetails().isImportedUniverse()) {
        // Update the DNS entry for primary cluster to mirror creation.
        Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
        createDnsManipulationTask(
                DnsManager.DnsCommandType.Delete, params().isForceDelete, universe)
            .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

        if (primaryCluster.userIntent.providerType.equals(CloudType.onprem)) {
          // Remove all nodes from load balancer.
          createManageLoadBalancerTasks(
              createLoadBalancerMap(
                  universe.getUniverseDetails(), null, new HashSet<>(universe.getNodes()), null));

          // Stop master and tservers.
          createStopServerTasks(universe.getNodes(), ServerType.MASTER, params().isForceDelete)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          createStopServerTasks(universe.getNodes(), ServerType.TSERVER, params().isForceDelete)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          if (universe.isYbcEnabled()) {
            createStopYbControllerTasks(universe.getNodes(), params().isForceDelete)
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          }
        }

        // Set the node states to Removing.
        createSetNodeStateTasks(universe.getNodes(), NodeDetails.NodeState.Terminating)
            .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
        // Create tasks to destroy the existing nodes.
        createDestroyServerTasks(
                universe,
                universe.getNodes(),
                params().isForceDelete,
                true /* delete node */,
                true /* deleteRootVolumes */,
                true /* skipDestroyPrecheck */)
            .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
      }

      // Create tasks to remove the universe entry from the Universe table.
      createRemoveUniverseEntryTask().setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(true /* removeFile */);

      if (params().isDeleteAssociatedCerts) {
        createDeleteCertificatesTaskGroup(universe.getUniverseDetails())
            .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
      }

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      Optional<Universe> optional = Universe.maybeGet(taskParams().getUniverseUUID());
      if (optional.isPresent()) {
        // If for any reason destroy fails we would just unlock the universe for update
        try {
          unlockUniverseForUpdate();
        } catch (Throwable t1) {
          // Ignore the error
        }
      }
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockXClusterUniverses(lockedXClusterUniversesUuidSet, params().isForceDelete);
    }
    log.info("Finished {} task.", getName());
  }

  public SubTaskGroup createRemoveUniverseEntryTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("RemoveUniverseEntry");
    Params params = new Params();
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.customerUUID = params().customerUUID;
    params.isForceDelete = params().isForceDelete;

    // Create the Ansible task to destroy the server.
    RemoveUniverseEntry task = createTask(RemoveUniverseEntry.class);
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDeleteCertificatesTaskGroup(
      UniverseDefinitionTaskParams universeDetails) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteCertificates");

    // Create the task to delete rootCerts.
    DeleteCertificate rootCertDeletiontask =
        createDeleteCertificateTask(params().customerUUID, universeDetails.rootCA);
    // Add it to the task list.
    if (rootCertDeletiontask != null) {
      subTaskGroup.addSubTask(rootCertDeletiontask);
    }

    if (!universeDetails.rootAndClientRootCASame) {
      // Create the task to delete clientRootCerts.
      DeleteCertificate clientRootCertDeletiontask =
          createDeleteCertificateTask(params().customerUUID, universeDetails.getClientRootCA());
      // Add it to the task list.
      if (clientRootCertDeletiontask != null) {
        subTaskGroup.addSubTask(clientRootCertDeletiontask);
      }
    }

    if (subTaskGroup.getSubTaskCount() > 0) {
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
    return subTaskGroup;
  }

  public DeleteCertificate createDeleteCertificateTask(UUID customerUUID, UUID certUUID) {
    DeleteCertificate.Params params = new DeleteCertificate.Params();
    params.customerUUID = customerUUID;
    params.certUUID = certUUID;
    DeleteCertificate task = createTask(DeleteCertificate.class);
    task.initialize(params);
    return task;
  }

  /**
   * It deletes all the xCluster configs that the universe belonging to the DestroyUniverse task is
   * one side of.
   *
   * <p>Note: it relies on the fact that the unlock universe operation on a universe that is not
   * locked by this task is a no-op.
   */
  protected void createDeleteXClusterConfigSubtasksAndLockOtherUniverses() {
    // XCluster configs whose other universe exists.
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByUniverseUuid(params().getUniverseUUID()).stream()
            .filter(
                xClusterConfig ->
                    xClusterConfig.getStatus()
                        != XClusterConfig.XClusterConfigStatusType.DeletedUniverse)
            .collect(Collectors.toList());

    // Set xCluster configs status to DeleteUniverse. We do not use the corresponding subtask to
    // do this because it might get into an error until that point, but we want to set this state
    // even when there is an error.
    xClusterConfigs.forEach(
        xClusterConfig -> {
          xClusterConfig.updateStatus(XClusterConfig.XClusterConfigStatusType.DeletedUniverse);
        });

    Map<UUID, List<XClusterConfig>> otherUniverseUuidToXClusterConfigsMap =
        xClusterConfigs.stream()
            .collect(
                Collectors.groupingBy(
                    xClusterConfig -> {
                      if (xClusterConfig
                          .getSourceUniverseUUID()
                          .equals(params().getUniverseUUID())) {
                        // Case 1: Delete xCluster configs where this universe is the source
                        // universe.
                        return xClusterConfig.getTargetUniverseUUID();
                      } else {
                        // Case 2: Delete xCluster configs where this universe is the target
                        // universe.
                        return xClusterConfig.getSourceUniverseUUID();
                      }
                    }));

    // Put all the universes in the locked list. The unlock operation is a no-op if the universe
    // does not get locked by this task.
    lockedXClusterUniversesUuidSet = new HashSet<>(otherUniverseUuidToXClusterConfigsMap.keySet());

    // Create the subtasks to delete the xCluster configs.
    otherUniverseUuidToXClusterConfigsMap.forEach(
        this::createDeleteXClusterConfigSubtasksAndLockOtherUniverse);
  }

  /**
   * It deletes all the xCluster configs between the universe in this task and the universe whose
   * UUID is specified in the {@code otherUniverseUuid} parameter.
   *
   * @param otherUniverseUuid The UUID of the universe on the other side of xCluster replication
   * @param xClusterConfigs The list of the xCluster configs to delete; they must be between the
   *     universe that belongs to this task and the universe whose UUID is specified in the {@code
   *     otherUniverseUuid} parameter
   */
  private void createDeleteXClusterConfigSubtasksAndLockOtherUniverse(
      UUID otherUniverseUuid, List<XClusterConfig> xClusterConfigs) {
    try {
      // Lock the other universe.
      if (lockUniverseIfExist(otherUniverseUuid, -1 /* expectedUniverseVersion */) == null) {
        log.info("Other universe is deleted; No further action is needed");
        return;
      }

      // Create the subtasks to delete all the xCluster configs.
      xClusterConfigs.forEach(
          xClusterConfig -> {
            DrConfig drConfig = xClusterConfig.getDrConfig();
            createDeleteXClusterConfigSubtasks(
                xClusterConfig, false /* keepEntry */, params().isForceDelete);
            if (Objects.nonNull(drConfig) && drConfig.getXClusterConfigs().size() == 1) {
              createDeleteDrConfigEntryTask(drConfig)
                  .setSubTaskGroupType(SubTaskGroupType.DeleteDrConfig);
            }
          });
      log.debug("Subtasks created to delete these xCluster configs: {}", xClusterConfigs);
    } catch (Exception e) {
      log.error(
          "{} hit error while creating subtasks for xCluster config deletion : {}",
          getName(),
          e.getMessage());
      // If this task is force delete, ignore errors.
      if (!params().isForceDelete) {
        throw new RuntimeException(e);
      } else {
        log.debug("Error ignored because isForceDelete is true");
      }
    }
  }

  protected void deleteSupportBundle(UUID universeUUID) {
    List<SupportBundle> supportBundles = SupportBundle.getAll(universeUUID);
    if (!supportBundles.isEmpty()) {
      for (SupportBundle supportBundle : supportBundles) {
        supportBundleUtil.deleteSupportBundle(supportBundle);
      }
    }
  }
}
