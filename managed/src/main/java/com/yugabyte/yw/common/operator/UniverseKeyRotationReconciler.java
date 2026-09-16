// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.controllers.handlers.UniverseActionsHandler;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.forms.EncryptionAtRestConfig.OpType;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.yugabyte.operator.v1alpha1.UniverseKeyRotation;
import io.yugabyte.operator.v1alpha1.UniverseKeyRotationStatus;
import java.time.Instant;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Reconciler for the UniverseKeyRotation custom resource. Each resource is a one-shot imperative
 * universe (data) key rotation: on create it submits a {@code SetUniverseKey} task against the
 * universe's currently active KMS config (so the backend treats it as a universe key rotation, not
 * a master key rotation), and tracks the outcome in the CR status. Listing the resources for a
 * universe gives the rotation history. Mirrors {@link PitrRestoreReconciler}.
 *
 * <p>A failed rotation is auto-retried (a fresh {@code SetUniverseKey} task is submitted) up to
 * {@link #MAX_ROTATION_RETRIES} times before the resource is marked terminally {@code Failed}. The
 * attempt count is persisted in {@code status.retryCount} so the bound survives operator restarts
 * (the in-memory work-queue retry count does not). This is safe because a failed rotation never
 * commits a new active universe key ({@code activateKeyRef} is the final step of the task), so each
 * retry converges to exactly one active key.
 */
@Slf4j
public class UniverseKeyRotationReconciler extends AbstractReconciler<UniverseKeyRotation> {

  private static final String STATE_RUNNING = "Running";
  private static final String STATE_RETRYING = "Retrying";
  private static final String STATE_SUCCEEDED = "Succeeded";
  private static final String STATE_FAILED = "Failed";

  // Maximum number of automatic retries after a rotation task failure before the resource is marked
  // terminally Failed. Retries are back-off spaced by the work queue (capped at 30 minutes).
  private static final int MAX_ROTATION_RETRIES = 5;

  private final UniverseActionsHandler universeActionsHandler;

  public UniverseKeyRotationReconciler(
      UniverseActionsHandler universeActionsHandler,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory) {
    super(client, informerFactory, UniverseKeyRotation.class, operatorUtils, namespace);
    this.universeActionsHandler = universeActionsHandler;
  }

  @Override
  protected void createActionReconcile(UniverseKeyRotation rotation, Customer cust)
      throws Exception {
    String resourceName = rotation.getMetadata().getName();
    try {
      // Idempotency: if a task is already tracked and is in progress or completed, do nothing.
      UniverseKeyRotationStatus status = rotation.getStatus();
      if (status != null && status.getTaskUUID() != null) {
        TaskInfo taskInfo = getCurrentTaskInfo(rotation);
        if (taskInfo == null) {
          log.info("Universe key rotation {} task not found, allowing retry", resourceName);
        } else if (taskInfo.getTaskState() == TaskInfo.State.Success
            || TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
          log.info(
              "Universe key rotation {} already has a task in progress or completed, skipping",
              resourceName);
          return;
        }
      }

      String universeName = rotation.getSpec().getUniverse();
      Universe universe =
          operatorUtils.getUniverseFromNameAndNamespace(
              cust.getId(), universeName, rotation.getMetadata().getNamespace());
      if (universe == null) {
        updateStatus(
            rotation, STATE_FAILED, "Universe '" + universeName + "' not found", null, true);
        return;
      }

      // Wait for any in-progress universe update to finish before submitting a rotation.
      if (universe.getUniverseDetails().updateInProgress) {
        log.debug(
            "Universe {} has an update in progress, requeuing universe key rotation {}",
            universeName,
            resourceName);
        workqueue.requeue(
            OperatorWorkQueue.getWorkQueueKey(rotation.getMetadata()),
            OperatorWorkQueue.ResourceAction.CREATE,
            false /* incrementRetry */);
        return;
      }

      // A disabled universe keeps its active universe key, so check the EAR flag on its own.
      UniverseDefinitionTaskParams details = universe.getUniverseDetails();
      if (details.encryptionAtRestConfig == null
          || !details.encryptionAtRestConfig.encryptionAtRestEnabled) {
        updateStatus(
            rotation,
            STATE_FAILED,
            "Universe '" + universeName + "' does not have encryption at rest enabled",
            null,
            true);
        return;
      }
      KmsHistory activeKey = EncryptionAtRestUtil.getActiveKey(universe.getUniverseUUID());
      if (activeKey == null) {
        updateStatus(
            rotation,
            STATE_FAILED,
            "Universe '" + universeName + "' does not have an active universe key",
            null,
            true);
        return;
      }
      UUID kmsConfigUUID = activeKey.getConfigUuid();
      if (KmsConfig.get(kmsConfigUUID) == null) {
        updateStatus(
            rotation,
            STATE_FAILED,
            "The active KMS config for universe '" + universeName + "' no longer exists",
            null,
            true);
        return;
      }

      log.info("Triggering universe key rotation for universe {}", universeName);
      EncryptionAtRestKeyParams keyParams = new EncryptionAtRestKeyParams();
      keyParams.setUniverseUUID(universe.getUniverseUUID());
      keyParams.encryptionAtRestConfig = new EncryptionAtRestConfig();
      // opType ENABLE with the currently active config UUID makes the backend treat this as a
      // universe key rotation (rather than enabling EAR or a master key rotation).
      keyParams.encryptionAtRestConfig.opType = OpType.ENABLE;
      keyParams.encryptionAtRestConfig.kmsConfigUUID = kmsConfigUUID;
      keyParams.encryptionAtRestConfig.encryptionAtRestEnabled = true;
      keyParams.setKubernetesResourceDetails(KubernetesResourceDetails.fromResource(rotation));

      UUID taskUUID = universeActionsHandler.setUniverseKey(cust, universe, keyParams);
      log.info("Universe key rotation {} triggered with task: {}", resourceName, taskUUID);
      updateStatus(rotation, STATE_RUNNING, "Universe key rotation task created", taskUUID, false);
    } catch (Exception e) {
      log.error("Failed to process create for universe key rotation {}", resourceName, e);
      updateStatus(
          rotation,
          STATE_FAILED,
          "Failed to trigger universe key rotation: " + e.getMessage(),
          null,
          true);
    }
  }

  // Universe key rotation is a one-shot operation; the spec is immutable so updates are no-ops.
  @Override
  protected void updateActionReconcile(UniverseKeyRotation rotation, Customer cust)
      throws Exception {
    log.debug(
        "Update action not supported for universe key rotation {}",
        rotation.getMetadata().getName());
  }

  // NO_OP reconcile handler
  // Case 1: task in an incomplete state: return (no requeue needed)
  // Case 2: task failed and retries remain: record a non-terminal Retrying status (bumping the
  //  durable retryCount) and requeue CREATE so a fresh rotation task is submitted after back-off
  // Case 3: task failed and retries are exhausted: write a terminal Failed status
  // Case 4: task succeeded: write a terminal Succeeded status
  // Case 5: no current task and not initialized: requeue CREATE
  @Override
  protected void noOpActionReconcile(UniverseKeyRotation rotation, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(rotation.getMetadata());
    String resourceName = rotation.getMetadata().getName();

    TaskInfo taskInfo = getCurrentTaskInfo(rotation);
    if (taskInfo != null) {
      if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
        log.debug("NoOp Action: universe key rotation {} task in progress", resourceName);
        return;
      } else if (taskInfo.getTaskState() != TaskInfo.State.Success) {
        int retryCount = currentRetryCount(rotation);
        if (retryCount >= MAX_ROTATION_RETRIES) {
          log.warn(
              "NoOp Action: universe key rotation {} failed after {} retries, marking Failed",
              resourceName,
              retryCount);
          updateStatus(
              rotation,
              STATE_FAILED,
              errorMessage(
                  "Universe key rotation failed after " + retryCount + " retries",
                  taskInfo.getErrorMessage()),
              taskInfo.getUuid(),
              true,
              retryCount);
          workqueue.resetRetries(mapKey);
        } else {
          int nextRetry = retryCount + 1;
          log.info(
              "NoOp Action: universe key rotation {} task failed, scheduling retry {}/{}",
              resourceName,
              nextRetry,
              MAX_ROTATION_RETRIES);
          updateStatus(
              rotation,
              STATE_RETRYING,
              errorMessage(
                  "Universe key rotation failed, retry " + nextRetry + " scheduled",
                  taskInfo.getErrorMessage()),
              taskInfo.getUuid(),
              false,
              nextRetry);
          workqueue.requeue(
              mapKey, OperatorWorkQueue.ResourceAction.CREATE, true /* incrementRetry */);
        }
      } else {
        log.info("Universe key rotation {} completed successfully", resourceName);
        updateStatus(
            rotation, STATE_SUCCEEDED, "Universe key rotation completed", taskInfo.getUuid(), true);
        workqueue.resetRetries(mapKey);
      }
    } else {
      log.debug(
          "NoOp Action: universe key rotation {} not initialized, requeuing Create", resourceName);
      workqueue.requeue(
          mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
    }
  }

  @Override
  protected void handleResourceDeletion(
      UniverseKeyRotation rotation, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(rotation.getMetadata());
    log.info("Deleting universe key rotation: {}", rotation.getMetadata().getName());
    workqueue.clearState(mapKey);
  }

  private static String errorMessage(String prefix, String taskError) {
    if (taskError == null || taskError.isBlank()) {
      return prefix;
    }
    return prefix + ": " + taskError;
  }

  private TaskInfo getCurrentTaskInfo(UniverseKeyRotation rotation) {
    if (rotation.getStatus() == null || rotation.getStatus().getTaskUUID() == null) {
      return null;
    }
    UUID taskUUID = UUID.fromString(rotation.getStatus().getTaskUUID());
    Optional<TaskInfo> optTaskInfo = TaskInfo.maybeGet(taskUUID);
    return optTaskInfo.orElse(null);
  }

  // Durable retry count persisted in the CR status (survives operator restarts). The generated
  // model types the CRD integer as a Long.
  private int currentRetryCount(UniverseKeyRotation rotation) {
    if (rotation.getStatus() == null || rotation.getStatus().getRetryCount() == null) {
      return 0;
    }
    return rotation.getStatus().getRetryCount().intValue();
  }

  private void updateStatus(
      UniverseKeyRotation rotation,
      String state,
      String message,
      UUID taskUUID,
      boolean completed) {
    updateStatus(rotation, state, message, taskUUID, completed, null /* retryCount */);
  }

  private void updateStatus(
      UniverseKeyRotation rotation,
      String state,
      String message,
      UUID taskUUID,
      boolean completed,
      Integer retryCount) {
    try {
      UniverseKeyRotation latest =
          resourceClient
              .inNamespace(rotation.getMetadata().getNamespace())
              .withName(rotation.getMetadata().getName())
              .get();
      if (latest == null) {
        return;
      }
      UniverseKeyRotationStatus status = latest.getStatus();
      if (status == null) {
        status = new UniverseKeyRotationStatus();
      }
      status.setState(state);
      status.setMessage(message);
      if (taskUUID != null) {
        status.setTaskUUID(taskUUID.toString());
      }
      if (retryCount != null) {
        status.setRetryCount(retryCount.longValue());
      }
      if (status.getRequestedAt() == null) {
        status.setRequestedAt(Instant.now().toString());
      }
      if (completed) {
        status.setCompletedAt(Instant.now().toString());
      }
      latest.setStatus(status);
      resourceClient
          .inNamespace(latest.getMetadata().getNamespace())
          .resource(latest)
          .updateStatus();
      log.debug("Updated status for universe key rotation CR {}", latest.getMetadata().getName());
    } catch (Exception e) {
      log.error(
          "Failed to update universe key rotation CR status for {}",
          rotation.getMetadata().getName(),
          e);
    }
  }
}
