// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.common.dr.DrConfigHelper.DrConfigTaskResult;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigReplaceReplicaForm;
import com.yugabyte.yw.forms.DrConfigRestartForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.DrConfig;
import io.yugabyte.operator.v1alpha1.DrConfigStatus;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class DrConfigReconciler extends AbstractReconciler<DrConfig> {
  private final DrConfigHelper drConfigHelper;
  private final SharedIndexInformer<StorageConfig> scInformer;
  private final Map<String, UUID> drConfigTaskMap;

  private final ResourceTracker resourceTracker = new ResourceTracker();

  public Set<KubernetesResourceDetails> getTrackedResources() {
    return resourceTracker.getTrackedResources();
  }

  public ResourceTracker getResourceTracker() {
    return resourceTracker;
  }

  public DrConfigReconciler(
      DrConfigHelper drConfigHelper,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory) {
    super(client, informerFactory, DrConfig.class, operatorUtils, namespace);
    this.drConfigHelper = drConfigHelper;
    this.scInformer = informerFactory.getSharedIndexInformer(StorageConfig.class, client);
    this.drConfigTaskMap = new HashMap<>();
  }

  @VisibleForTesting
  UUID getDrConfigTaskMapValue(String key) {
    return this.drConfigTaskMap.getOrDefault(key, null);
  }

  @Override
  protected void createActionReconcile(DrConfig drConfig, Customer cust) throws Exception {

    String resourceName = drConfig.getMetadata().getName();
    try {
      DrConfigStatus status = drConfig.getStatus();
      if (status != null && status.getResourceUUID() != null) {
        OperatorUtils.maybeAddYbaResourceId(
            drConfig, UUID.fromString(status.getResourceUUID()), resourceClient);
        log.info("DR Config {} is already initialized", drConfig.getMetadata().getName());
        return;
      }

      String resourceNamespace = drConfig.getMetadata().getNamespace();
      log.info("Creating DR config: {}", resourceName);

      // Validate spec universes resolve before we stamp a finalizer or create the task.
      String specSourceName = drConfig.getSpec().getSourceUniverse();
      String specTargetName = drConfig.getSpec().getTargetUniverse();
      Universe specSourceUniverse =
          operatorUtils.getUniverseFromNameAndNamespace(
              cust.getId(), specSourceName, resourceNamespace);
      Universe specTargetUniverse =
          operatorUtils.getUniverseFromNameAndNamespace(
              cust.getId(), specTargetName, resourceNamespace);
      if (specSourceUniverse == null || specTargetUniverse == null) {
        String reason =
            buildMissingUniverseReason(
                specSourceName, specSourceUniverse, specTargetName, specTargetUniverse);
        log.warn("DR config {} cannot be created: {}", resourceName, reason);
        updateDrConfigCrStatus(drConfig, "Failed to create task. " + reason + ".", null);
        return;
      }

      // Set finalizer if not already set
      ObjectMeta objectMeta = drConfig.getMetadata();
      if (CollectionUtils.isEmpty(objectMeta.getFinalizers())) {
        objectMeta.setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
        resourceClient.inNamespace(resourceNamespace).withName(resourceName).patch(drConfig);
      }

      DrConfigCreateForm drConfigCreateForm =
          operatorUtils.getDrConfigCreateFormFromCr(drConfig, scInformer);

      UUID customerUUID = cust.getUuid();

      DrConfigTaskResult result =
          drConfigHelper.createDrConfigTask(customerUUID, drConfigCreateForm);
      UUID taskUUID = result.taskUuid();

      log.info("DR config {} creation triggered with task: {}", resourceName, taskUUID);
      if (taskUUID != null) {
        drConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata()), taskUUID);
        updateDrConfigCrStatus(drConfig, "DR config creation task created", taskUUID);
      }

    } catch (Exception e) {
      log.error("Failed to process create for DR config {}", resourceName, e);
      updateDrConfigCrStatus(drConfig, "Failed to create task. " + e.getMessage(), null);
    }
  }

  // Handles multiple cases by comparing CR spec with database model:
  // Case 1: Failover        - targetUniverse is empty string (non-halted only)
  // Case 2: Restart         - DR config is halted; restart in spec direction
  // Case 3: Switchover      - source/target swapped (non-halted only)
  // Case 4: Replace replica - source unchanged, target changed (non-halted only)
  // Case 5: Pause/Resume    - paused field changed
  // Case 6: Database list update
  // Case 7: No change needed
  @Override
  protected void updateActionReconcile(DrConfig drConfig, Customer cust) throws Exception {

    String resourceName = drConfig.getMetadata().getName();
    try {

      DrConfigStatus status = drConfig.getStatus();

      if (status == null || status.getResourceUUID() == null) {
        log.debug("DR config {} has no status, ignoring update", resourceName);
        updateDrConfigCrStatus(drConfig, "Failed to create task. The CR has no status.", null);
        return;
      }

      UUID drConfigUUID = UUID.fromString(status.getResourceUUID());
      Optional<com.yugabyte.yw.models.DrConfig> optionalDrConfig =
          com.yugabyte.yw.models.DrConfig.maybeGet(drConfigUUID);

      if (!optionalDrConfig.isPresent()) {
        log.debug("DR config {} does not exist, ignoring update", resourceName);
        updateDrConfigCrStatus(
            drConfig, "Failed to create task. The DR config does not exist.", null);
        return;
      }

      com.yugabyte.yw.models.DrConfig drConfigModel = optionalDrConfig.get();
      XClusterConfig xClusterConfig = drConfigModel.getActiveXClusterConfig();

      if (xClusterConfig == null) {
        log.debug("DR config {} has no active xCluster config, ignoring update", resourceName);
        updateDrConfigCrStatus(
            drConfig, "Failed to create task. The DR config has no active xCluster config.", null);
        return;
      }

      String specTargetName = drConfig.getSpec().getTargetUniverse();
      String specSourceName = drConfig.getSpec().getSourceUniverse();
      String namespace = drConfig.getMetadata().getNamespace();

      // Case 1: Failover - targetUniverse is empty string
      if (specTargetName != null && specTargetName.isEmpty()) {
        if (!drConfigModel.isHalted()) {
          log.info("DR config {} requires failover (target is empty)", resourceName);
          handleFailover(drConfig, cust, status);
          return;
        }
        log.debug("DR config {} already in halted state, skipping failover", resourceName);
        return;
      }

      // Resolve spec universe names (which are YBUniverse CR names) to YBA universe UUIDs.
      // String-comparing against Universe.getName() is unsafe because the YBA-internal
      // universe name is suffixed with a creation epoch (e.g. "dr-source-1507547201"),
      // while spec.sourceUniverse holds the short CR name ("dr-source").
      Universe specSourceUniverse =
          operatorUtils.getUniverseFromNameAndNamespace(cust.getId(), specSourceName, namespace);
      Universe specTargetUniverse =
          operatorUtils.getUniverseFromNameAndNamespace(cust.getId(), specTargetName, namespace);

      if (specSourceUniverse == null || specTargetUniverse == null) {
        String reason =
            buildMissingUniverseReason(
                specSourceName, specSourceUniverse, specTargetName, specTargetUniverse);
        log.warn("DR config {} cannot be updated: {}", resourceName, reason);
        updateDrConfigCrStatus(drConfig, "Failed to create task. " + reason + ".", null);
        return;
      }

      UUID specSourceUuid = specSourceUniverse.getUniverseUUID();
      UUID specTargetUuid = specTargetUniverse.getUniverseUUID();
      UUID currentSourceUuid = xClusterConfig.getSourceUniverseUUID();
      UUID currentTargetUuid = xClusterConfig.getTargetUniverseUUID();

      // Case 2: Halted DR config - only restart is valid.
      // After failover, xClusterConfig source/target reflect the pre-failover direction
      // and are not a reliable signal of intent; switchover and replace-replica must
      // not run on a halted config.
      if (drConfigModel.isHalted()) {
        log.info(
            "DR config {} requires restart (halted, spec source={}, target={})",
            resourceName,
            specSourceName,
            specTargetName);
        handleRestart(drConfig, cust, drConfigModel);
        return;
      }

      // Case 3: Switchover - source and target swapped (non-halted only)
      if (specSourceUuid.equals(currentTargetUuid) && specTargetUuid.equals(currentSourceUuid)) {
        log.info("DR config {} requires switchover (source/target swapped)", resourceName);
        handleSwitchover(drConfig, cust, status);
        return;
      }

      // Case 4: Replace replica - source unchanged, target changed (non-halted only;
      // halted was handled above and routed to restart).
      if (specSourceUuid.equals(currentSourceUuid) && !specTargetUuid.equals(currentTargetUuid)) {
        log.info("DR config {} requires replace replica (target changed)", resourceName);
        handleReplaceReplica(drConfig, cust, drConfigModel, xClusterConfig);
        return;
      }

      // Case 5: Pause/Resume - check if paused state differs
      if (requiresPauseResumeUpdate(drConfig, xClusterConfig)) {
        Boolean specPaused = drConfig.getSpec().getPaused();
        if (specPaused != null && specPaused) {
          log.info("DR config {} requires pause", resourceName);
          handlePause(drConfig, cust, drConfigModel);
        } else {
          log.info("DR config {} requires resume", resourceName);
          handleResume(drConfig, cust, drConfigModel);
        }
        return;
      }

      // Case 6: Database list update - compare with current DB state
      if (requiresDatabaseUpdate(drConfig, drConfigModel)) {
        log.info("DR config {} requires database update", resourceName);
        handleDatabaseUpdate(drConfig, cust, status);
        return;
      }

      // Case 7: No change needed
      log.debug("DR config {} does not require any update", resourceName);
    } catch (Exception e) {
      log.error("Failed to process update for DR config {}", resourceName, e);
      updateDrConfigCrStatus(drConfig, "Failed to create task. " + e.getMessage(), null);
    }
  }

  // NO_OP reconcile handler
  // Case 1: Currently running task in incomplete state: NO_OP
  // Case 2: Currently running task in failed state: CREATE/UPDATE based on task type
  // Case 3: Currently running task in Success state: Reset retries and queue UPDATE if required
  // Case 4: No current task and DR config not present: Requeue CREATE
  // Case 5: No current task and DR config requires update: Requeue UPDATE
  @Override
  protected void noOpActionReconcile(DrConfig drConfig, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata());
    String resourceName = drConfig.getMetadata().getName();
    boolean resourceExists =
        drConfig.getStatus() != null && drConfig.getStatus().getResourceUUID() != null;

    if (!resourceExists) {
      // Check for tracked tasks below before assuming fresh create
      if (getCurrentTaskInfo(drConfig) == null) {
        log.debug("NoOp Action: DR Config {} not found, requeuing Create", resourceName);
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
        return;
      }
    }

    UUID drConfigUUID = null;
    Optional<com.yugabyte.yw.models.DrConfig> optionalDrConfig = Optional.empty();
    if (resourceExists) {
      drConfigUUID = UUID.fromString(drConfig.getStatus().getResourceUUID());
      optionalDrConfig = com.yugabyte.yw.models.DrConfig.maybeGet(drConfigUUID);
    }

    TaskInfo taskInfo = getCurrentTaskInfo(drConfig);
    if (taskInfo != null) {
      if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
        log.debug("NoOp Action: DR Config {} task in progress, requeuing no-op", resourceName);
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false /* incrementRetry */);
      } else if (taskInfo.getTaskState() != TaskInfo.State.Success) {
        // Task failed - determine what to retry based on task type
        TaskType taskType = taskInfo.getTaskType();
        if (taskType == TaskType.CreateDrConfig) {
          log.debug("NoOp Action: DR Config {} create task failed, requeuing Create", resourceName);
          workqueue.requeue(
              mapKey, OperatorWorkQueue.ResourceAction.CREATE, true /* incrementRetry */);
        } else {
          log.debug("NoOp Action: DR Config {} update task failed, requeuing Update", resourceName);
          workqueue.requeue(
              mapKey, OperatorWorkQueue.ResourceAction.UPDATE, true /* incrementRetry */);
        }
        drConfigTaskMap.remove(mapKey);
      } else {
        // Task succeeded
        workqueue.resetRetries(mapKey);
        drConfigTaskMap.remove(mapKey);
        // Check if further updates are needed
        if (optionalDrConfig.isPresent()) {
          com.yugabyte.yw.models.DrConfig drConfigModel = optionalDrConfig.get();
          drConfigModel.refresh();
          if (requiresUpdate(drConfig, drConfigModel)) {
            workqueue.requeue(
                mapKey, OperatorWorkQueue.ResourceAction.UPDATE, false /* incrementRetry */);
          }
        }
      }
      return;
    }

    // No current task running
    if (!optionalDrConfig.isPresent()) {
      log.debug("NoOp Action: DR Config {} model not found, requeuing Create", resourceName);
      workqueue.requeue(
          mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
      return;
    }

    com.yugabyte.yw.models.DrConfig drConfigModel = optionalDrConfig.get();
    XClusterConfig xClusterConfig = drConfigModel.getActiveXClusterConfig();

    if (xClusterConfig == null) {
      log.debug("NoOp Action: DR Config {} has no active xCluster config", resourceName);
      return;
    }

    drConfigModel.refresh();
    // Check if any update is required
    if (requiresUpdate(drConfig, drConfigModel)) {
      if (specHasUnresolvableUniverse(drConfig) && statusAlreadyReflectsMissingUniverse(drConfig)) {
        log.debug(
            "NoOp Action: DR Config {} has unresolvable universes and status already"
                + " reflects this; skipping requeue",
            resourceName);
        workqueue.resetRetries(mapKey);
        return;
      }
      log.debug("NoOp Action: DR Config {} requires update, requeuing Update", resourceName);
      workqueue.requeue(
          mapKey, OperatorWorkQueue.ResourceAction.UPDATE, false /* incrementRetry */);
    } else {
      workqueue.resetRetries(mapKey);
    }
  }

  /**
   * Check if any update is required (failover, switchover, replace replica, restart, pause/resume,
   * or database update).
   */
  private boolean requiresUpdate(DrConfig drConfig, com.yugabyte.yw.models.DrConfig drConfigModel) {
    XClusterConfig xClusterConfig = drConfigModel.getActiveXClusterConfig();
    if (xClusterConfig == null) {
      return false;
    }

    String specTargetName = drConfig.getSpec().getTargetUniverse();
    String specSourceName = drConfig.getSpec().getSourceUniverse();
    String namespace = drConfig.getMetadata().getNamespace();

    // Failover: target is empty string on a non-halted config
    if (specTargetName != null && specTargetName.isEmpty()) {
      return !drConfigModel.isHalted() && xClusterConfig.getTargetUniverseUUID() != null;
    }

    // Halted: any non-empty target means restart is required.
    if (drConfigModel.isHalted()) {
      return specTargetName != null && !specTargetName.isEmpty();
    }

    Customer cust;
    try {
      cust = operatorUtils.getOperatorCustomer();
    } catch (Exception e) {
      log.warn("requiresUpdate: failed to get operator customer: {}", e.getMessage());
      return false;
    }

    Universe specSourceUniverse;
    Universe specTargetUniverse;
    try {
      specSourceUniverse =
          operatorUtils.getUniverseFromNameAndNamespace(cust.getId(), specSourceName, namespace);
      specTargetUniverse =
          operatorUtils.getUniverseFromNameAndNamespace(cust.getId(), specTargetName, namespace);
    } catch (Exception e) {
      log.warn(
          "requiresUpdate: failed to resolve spec universes for DR config {}: {}",
          drConfig.getMetadata().getName(),
          e.getMessage());
      return false;
    }
    if (specSourceUniverse == null || specTargetUniverse == null) {
      return true;
    }

    UUID specSourceUuid = specSourceUniverse.getUniverseUUID();
    UUID specTargetUuid = specTargetUniverse.getUniverseUUID();
    UUID currentSourceUuid = xClusterConfig.getSourceUniverseUUID();
    UUID currentTargetUuid = xClusterConfig.getTargetUniverseUUID();

    // Switchover (non-halted only)
    if (specSourceUuid.equals(currentTargetUuid) && specTargetUuid.equals(currentSourceUuid)) {
      return true;
    }

    // Replace replica (non-halted only)
    if (specSourceUuid.equals(currentSourceUuid) && !specTargetUuid.equals(currentTargetUuid)) {
      return true;
    }

    if (requiresPauseResumeUpdate(drConfig, xClusterConfig)) {
      return true;
    }

    return requiresDatabaseUpdate(drConfig, drConfigModel);
  }

  /**
   * Check if pause/resume update is required by comparing CR spec paused field with xCluster
   * config.
   */
  private boolean requiresPauseResumeUpdate(DrConfig drConfig, XClusterConfig xClusterConfig) {
    Boolean specPaused = drConfig.getSpec().getPaused();
    // Default to false if not specified
    boolean isPausedInSpec = specPaused != null && specPaused;
    boolean isPausedInDb = xClusterConfig.isPaused();
    return isPausedInSpec != isPausedInDb;
  }

  // Delete the DR config
  @Override
  protected void handleResourceDeletion(
      DrConfig drConfig, Customer cust, OperatorWorkQueue.ResourceAction action) throws Exception {

    String mapKey = OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata());
    String resourceName = drConfig.getMetadata().getName();
    DrConfigStatus status = drConfig.getStatus();

    log.info("Deleting DR config: {}", resourceName);

    // Remove finalizer if no status (resource was never fully created)
    if (status == null || status.getResourceUUID() == null) {
      log.info("DR config {} has no status, removing finalizer", resourceName);
      try {
        operatorUtils.removeFinalizer(drConfig, resourceClient);
        workqueue.clearState(mapKey);
      } catch (Exception e) {
        log.error("Failed to remove finalizer for DR config: {}", resourceName, e);
      }
      return;
    }

    UUID drConfigUUID = UUID.fromString(status.getResourceUUID());
    Optional<com.yugabyte.yw.models.DrConfig> optDrConfig =
        com.yugabyte.yw.models.DrConfig.maybeGet(drConfigUUID);
    boolean drConfigRemoved = false;

    if (optDrConfig.isPresent()) {
      com.yugabyte.yw.models.DrConfig drConfigModel = optDrConfig.get();
      try {
        // Check if owner universe is being deleted by checking the YBUniverse CR
        boolean ownerUniverseBeingDeleted = false;
        XClusterConfig xClusterConfig = drConfigModel.getActiveXClusterConfig();

        if (xClusterConfig != null) {
          // Check source universe YBUniverse CR
          Universe sourceUniverse =
              Universe.maybeGet(xClusterConfig.getSourceUniverseUUID()).orElse(null);
          if (sourceUniverse != null && sourceUniverse.getUniverseDetails() != null) {
            KubernetesResourceDetails sourceDetails =
                sourceUniverse.getUniverseDetails().getKubernetesResourceDetails();
            if (sourceDetails != null) {
              YBUniverse sourceYBUniverse = operatorUtils.getYBUniverse(sourceDetails);
              if (sourceYBUniverse == null
                  || sourceYBUniverse.getMetadata().getDeletionTimestamp() != null) {
                ownerUniverseBeingDeleted = true;
              }
            }
          } else {
            // Source universe doesn't exist anymore
            ownerUniverseBeingDeleted = true;
          }
        }

        if (ownerUniverseBeingDeleted) {
          // If owner universe is being deleted, delete DR config from the database directly
          log.info("Owner universe is being deleted, deleting DR config {} directly", resourceName);
          drConfigRemoved = drConfigModel.delete();
        } else {
          // Use delete task
          TaskInfo taskInfo = getCurrentTaskInfo(drConfig);

          if (taskInfo != null) {
            if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
              // Task still in progress, requeue to check again
              workqueue.requeue(
                  mapKey, OperatorWorkQueue.ResourceAction.DELETE, false /* incrementRetry */);
              return;
            } else if (taskInfo.getTaskState() != TaskInfo.State.Success) {
              drConfigTaskMap.remove(mapKey);
              // Retrying because the task failed
              log.info("Delete task failed for DR config {}, retrying", resourceName);
              workqueue.requeue(
                  mapKey, OperatorWorkQueue.ResourceAction.DELETE, true /* incrementRetry */);
              return;
            }
            // Task succeeded, fall through to remove finalizer
            drConfigTaskMap.remove(mapKey);
            drConfigRemoved = true;
          } else {
            // No task running, start delete task
            log.info("Starting delete task for DR config {}", resourceName);
            UUID taskUUID =
                drConfigHelper.deleteDrConfigTask(
                    cust.getUuid(), drConfigUUID, true /* forceDelete */);
            if (taskUUID != null) {
              drConfigTaskMap.put(mapKey, taskUUID);
              updateDrConfigCrStatus(drConfig, "DR config deletion task created.", taskUUID);
            }
            workqueue.requeue(
                mapKey, OperatorWorkQueue.ResourceAction.DELETE, false /* incrementRetry */);
            return;
          }
        }
      } catch (Exception e) {
        log.error("Failed to delete DR config {}, will retry", resourceName, e);
        updateDrConfigCrStatus(drConfig, "Failed to create task. " + e.getMessage(), null);
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.DELETE, true /* incrementRetry */);
        return;
      }
    }

    if (drConfigRemoved || !optDrConfig.isPresent()) {
      log.info("Removing finalizer for DR config {}", resourceName);
      try {
        operatorUtils.removeFinalizer(drConfig, resourceClient);
        workqueue.clearState(mapKey);
      } catch (Exception e) {
        log.error("Removing finalizer failed for {}, will retry", resourceName, e);
      }
    }
  }

  /**
   * Check if the database list in the CR spec differs from the current DR config model. Delegates
   * to operatorUtils for precise name-to-ID resolution.
   */
  private boolean requiresDatabaseUpdate(
      DrConfig drConfig, com.yugabyte.yw.models.DrConfig drConfigModel) {
    XClusterConfig xClusterConfig = drConfigModel.getActiveXClusterConfig();
    if (xClusterConfig == null) {
      return false;
    }
    return operatorUtils.requiresDrConfigDatabaseUpdate(drConfig, xClusterConfig);
  }

  private void handleFailover(DrConfig drConfig, Customer cust, DrConfigStatus status)
      throws Exception {
    String resourceName = drConfig.getMetadata().getName();
    UUID drConfigUUID = UUID.fromString(status.getResourceUUID());
    UUID customerUUID = cust.getUuid();

    DrConfigFailoverForm failoverForm = operatorUtils.getDrConfigFailoverFormFromCr(drConfig);

    UUID taskUUID = drConfigHelper.failoverDrConfigTask(customerUUID, drConfigUUID, failoverForm);

    log.info("DR config {} failover triggered with task: {}", resourceName, taskUUID);
    if (taskUUID != null) {
      drConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata()), taskUUID);
      updateDrConfigCrStatus(drConfig, "DR config failover task created.", taskUUID);
    }
  }

  private void handleSwitchover(DrConfig drConfig, Customer cust, DrConfigStatus status)
      throws Exception {
    String resourceName = drConfig.getMetadata().getName();
    UUID drConfigUUID = UUID.fromString(status.getResourceUUID());
    UUID customerUUID = cust.getUuid();

    DrConfigSwitchoverForm switchoverForm = operatorUtils.getDrConfigSwitchoverFormFromCr(drConfig);

    UUID taskUUID =
        drConfigHelper.switchoverDrConfigTask(customerUUID, drConfigUUID, switchoverForm);

    log.info("DR config {} switchover triggered with task: {}", resourceName, taskUUID);
    if (taskUUID != null) {
      drConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata()), taskUUID);
      updateDrConfigCrStatus(drConfig, "DR config switchover task created.", taskUUID);
    }
  }

  private void handleDatabaseUpdate(DrConfig drConfig, Customer cust, DrConfigStatus status)
      throws Exception {
    String resourceName = drConfig.getMetadata().getName();
    UUID drConfigUUID = UUID.fromString(status.getResourceUUID());
    UUID customerUUID = cust.getUuid();

    DrConfigSetDatabasesForm drConfigSetDatabasesForm =
        operatorUtils.getDrConfigSetDatabasesFormFromCr(drConfig);

    UUID taskUUID =
        drConfigHelper.setDatabasesTask(customerUUID, drConfigUUID, drConfigSetDatabasesForm);

    log.info("DR config {} set databases triggered with task: {}", resourceName, taskUUID);
    if (taskUUID != null) {
      drConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata()), taskUUID);
      updateDrConfigCrStatus(drConfig, "DR config set databases task created.", taskUUID);
    }
  }

  private void handleRestart(
      DrConfig drConfig, Customer cust, com.yugabyte.yw.models.DrConfig drConfigModel)
      throws Exception {
    String resourceName = drConfig.getMetadata().getName();
    UUID drConfigUUID = drConfigModel.getUuid();
    UUID customerUUID = cust.getUuid();

    DrConfigRestartForm restartForm =
        operatorUtils.getDrConfigRestartFormFromCr(drConfig, scInformer);

    UUID taskUUID =
        drConfigHelper.restartDrConfigTask(
            customerUUID, drConfigUUID, restartForm, false /* isForceDelete */);

    log.info("DR config {} restart triggered with task: {}", resourceName, taskUUID);
    if (taskUUID != null) {
      drConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata()), taskUUID);
    }
  }

  private void handleReplaceReplica(
      DrConfig drConfig,
      Customer cust,
      com.yugabyte.yw.models.DrConfig drConfigModel,
      XClusterConfig xClusterConfig)
      throws Exception {
    String resourceName = drConfig.getMetadata().getName();

    // Get current universes
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());

    DrConfigReplaceReplicaForm replaceReplicaForm =
        operatorUtils.getDrConfigReplaceReplicaFormFromCr(drConfig, scInformer);

    UUID taskUUID =
        drConfigHelper.replaceReplicaTask(
            cust,
            drConfigModel,
            xClusterConfig,
            sourceUniverse,
            targetUniverse,
            replaceReplicaForm);

    log.info("DR config {} replace replica triggered with task: {}", resourceName, taskUUID);
    if (taskUUID != null) {
      drConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata()), taskUUID);
    }
  }

  private void handlePause(
      DrConfig drConfig, Customer cust, com.yugabyte.yw.models.DrConfig drConfigModel)
      throws Exception {
    String resourceName = drConfig.getMetadata().getName();
    UUID drConfigUUID = drConfigModel.getUuid();
    UUID customerUUID = cust.getUuid();

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Paused";
    editFormData.setKubernetesResourceDetails(KubernetesResourceDetails.fromResource(drConfig));

    UUID taskUUID =
        drConfigHelper.toggleDrState(
            customerUUID, drConfigUUID, editFormData, CustomerTask.TaskType.Pause);

    log.info("DR config {} pause triggered with task: {}", resourceName, taskUUID);
    if (taskUUID != null) {
      drConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata()), taskUUID);
    }
  }

  private void handleResume(
      DrConfig drConfig, Customer cust, com.yugabyte.yw.models.DrConfig drConfigModel)
      throws Exception {
    String resourceName = drConfig.getMetadata().getName();
    UUID drConfigUUID = drConfigModel.getUuid();
    UUID customerUUID = cust.getUuid();

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = "Running";
    editFormData.setKubernetesResourceDetails(KubernetesResourceDetails.fromResource(drConfig));

    UUID taskUUID =
        drConfigHelper.toggleDrState(
            customerUUID, drConfigUUID, editFormData, CustomerTask.TaskType.Resume);

    log.info("DR config {} resume triggered with task: {}", resourceName, taskUUID);
    if (taskUUID != null) {
      drConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata()), taskUUID);
    }
  }

  private TaskInfo getCurrentTaskInfo(DrConfig drConfig) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata());
    UUID taskUUID = drConfigTaskMap.get(mapKey);
    if (taskUUID != null) {
      Optional<TaskInfo> optTaskInfo = TaskInfo.maybeGet(taskUUID);
      if (optTaskInfo.isPresent()) {
        return optTaskInfo.get();
      }
    }
    return null;
  }

  private static String buildMissingUniverseReason(
      String sourceName, Universe sourceUniverse, String targetName, Universe targetUniverse) {
    if (sourceUniverse == null && targetUniverse == null) {
      return "source universe '"
          + sourceName
          + "' and target universe '"
          + targetName
          + "' do not exist";
    }
    if (sourceUniverse == null) {
      return "source universe '" + sourceName + "' does not exist";
    }
    return "target universe '" + targetName + "' does not exist";
  }

  private void updateDrConfigCrStatus(DrConfig drConfig, String message, UUID taskUUID) {
    try {
      DrConfigStatus status = drConfig.getStatus();
      if (status == null) {
        status = new DrConfigStatus();
      }
      status.setMessage("Operation Status: " + message);
      if (taskUUID != null) {
        status.setTaskUUID(taskUUID.toString());
      }
      drConfig.setStatus(status);
      resourceClient
          .inNamespace(drConfig.getMetadata().getNamespace())
          .resource(drConfig)
          .updateStatus();
    } catch (Exception e) {
      log.error("Failed to update DR config CR status for {}", drConfig.getMetadata().getName(), e);
    }
  }

  private boolean specHasUnresolvableUniverse(DrConfig drConfig) {
    try {
      Customer cust = operatorUtils.getOperatorCustomer();
      String ns = drConfig.getMetadata().getNamespace();
      Universe source =
          operatorUtils.getUniverseFromNameAndNamespace(
              cust.getId(), drConfig.getSpec().getSourceUniverse(), ns);
      Universe target =
          operatorUtils.getUniverseFromNameAndNamespace(
              cust.getId(), drConfig.getSpec().getTargetUniverse(), ns);
      return source == null || target == null;
    } catch (Exception e) {
      return false;
    }
  }

  private boolean statusAlreadyReflectsMissingUniverse(DrConfig drConfig) {
    DrConfigStatus status = drConfig.getStatus();
    if (status == null || status.getMessage() == null) {
      return false;
    }
    return status.getMessage().contains("does not exist");
  }
}
