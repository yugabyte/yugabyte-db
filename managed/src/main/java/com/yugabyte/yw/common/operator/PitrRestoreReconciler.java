// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.pitr.PitrConfigHelper;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TaskInfo;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.yugabyte.operator.v1alpha1.PitrRestore;
import io.yugabyte.operator.v1alpha1.PitrRestoreStatus;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PitrRestoreReconciler extends AbstractReconciler<PitrRestore> {

  private final PitrConfigHelper pitrConfigHelper;

  public PitrRestoreReconciler(
      PitrConfigHelper pitrConfigHelper,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory) {
    super(client, informerFactory, PitrRestore.class, operatorUtils, namespace);
    this.pitrConfigHelper = pitrConfigHelper;
  }

  @Override
  protected void createActionReconcile(PitrRestore pitrRestore, Customer cust) throws Exception {
    String resourceName = pitrRestore.getMetadata().getName();
    try {
      PitrRestoreStatus status = pitrRestore.getStatus();
      if (status != null && status.getTaskUUID() != null) {
        TaskInfo taskInfo = getCurrentTaskInfo(pitrRestore);
        if (taskInfo == null) {
          log.info("PITR restore {} task not found, allowing retry", resourceName);
        } else if (taskInfo.getTaskState() == TaskInfo.State.Success
            || TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
          log.info(
              "PITR restore {} already has a task in progress or completed, skipping",
              resourceName);
          return;
        }
      }

      log.info("Creating PITR restore: {}", resourceName);

      RestoreSnapshotScheduleParams taskParams;
      try {
        taskParams = operatorUtils.getRestoreSnapshotScheduleParamsFromCr(pitrRestore);
      } catch (Exception e) {
        log.error("Failed to parse PITR restore params for {}", resourceName, e);
        updatePitrRestoreCrStatus(
            pitrRestore, "Failed to process PITR restore: " + e.getMessage(), null);
        return;
      }

      UUID taskUUID =
          pitrConfigHelper.restorePitrConfig(
              cust.getUuid(), taskParams.getUniverseUUID(), taskParams);

      log.info("PITR restore {} triggered with task: {}", resourceName, taskUUID);
      if (taskUUID != null) {
        updatePitrRestoreCrStatus(pitrRestore, "PITR restore task created", taskUUID);
      }
    } catch (Exception e) {
      log.error("Failed to process create for PITR restore {}", resourceName, e);
      updatePitrRestoreCrStatus(
          pitrRestore, "Failed to process PITR restore: " + e.getMessage(), null);
    }
  }

  // PITR restore is a one-shot operation - updates are not supported since
  // all spec fields are immutable (enforced via x-kubernetes-validations).
  @Override
  protected void updateActionReconcile(PitrRestore pitrRestore, Customer cust) throws Exception {
    log.debug(
        "Update action not supported for PITR restore {}", pitrRestore.getMetadata().getName());
  }

  // NO_OP reconcile handler
  // Case 1: Currently running task in incomplete state: return (no requeue needed)
  // Case 2: Currently running task in failed state: requeue CREATE to retry
  // Case 3: Currently running task in success state: update status to COMPLETE
  // Case 4: No current task and resource not initialized: requeue CREATE
  @Override
  protected void noOpActionReconcile(PitrRestore pitrRestore, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(pitrRestore.getMetadata());
    String resourceName = pitrRestore.getMetadata().getName();

    TaskInfo taskInfo = getCurrentTaskInfo(pitrRestore);
    if (taskInfo != null) {
      if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
        log.debug("NoOp Action: PITR restore {} task in progress", resourceName);
        return;
      } else if (taskInfo.getTaskState() != TaskInfo.State.Success) {
        log.debug(
            "NoOp Action: PITR restore {} task failed, requeuing Create to retry", resourceName);
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.CREATE, true /* incrementRetry */);
      } else {
        // Task succeeded
        log.info("PITR restore {} completed successfully", resourceName);
        workqueue.resetRetries(mapKey);
      }
    } else {
      log.debug("NoOp Action: PITR restore {} not initialized, requeuing Create", resourceName);
      workqueue.requeue(
          mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
    }
  }

  @Override
  protected void handleResourceDeletion(
      PitrRestore pitrRestore, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(pitrRestore.getMetadata());
    log.info("Deleting PITR restore: {}", pitrRestore.getMetadata().getName());
    workqueue.clearState(mapKey);
  }

  private TaskInfo getCurrentTaskInfo(PitrRestore pitrRestore) {
    if (pitrRestore.getStatus() == null || pitrRestore.getStatus().getTaskUUID() == null) {
      return null;
    }
    UUID taskUUID = UUID.fromString(pitrRestore.getStatus().getTaskUUID());
    Optional<TaskInfo> optTaskInfo = TaskInfo.maybeGet(taskUUID);
    return optTaskInfo.orElse(null);
  }

  private void updatePitrRestoreCrStatus(PitrRestore pitrRestore, String message, UUID taskUUID) {
    try {
      PitrRestoreStatus status = pitrRestore.getStatus();
      if (status == null) {
        status = new PitrRestoreStatus();
      }
      status.setMessage(message);
      if (taskUUID != null) {
        status.setTaskUUID(taskUUID.toString());
      }
      pitrRestore.setStatus(status);
      resourceClient
          .inNamespace(pitrRestore.getMetadata().getNamespace())
          .resource(pitrRestore)
          .updateStatus();
    } catch (Exception e) {
      log.error(
          "Failed to update PITR restore CR status for {}", pitrRestore.getMetadata().getName(), e);
    }
  }
}
