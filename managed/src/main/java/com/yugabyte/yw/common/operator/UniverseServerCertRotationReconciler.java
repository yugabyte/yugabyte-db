// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.yugabyte.operator.v1alpha1.UniverseServerCertRotation;
import io.yugabyte.operator.v1alpha1.UniverseServerCertRotationStatus;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.time.Instant;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class UniverseServerCertRotationReconciler
    extends AbstractReconciler<UniverseServerCertRotation> {

  private static final String STATE_RUNNING = "Running";
  private static final String STATE_RETRYING = "Retrying";
  private static final String STATE_SUCCEEDED = "Succeeded";
  private static final String STATE_FAILED = "Failed";

  private final UpgradeUniverseHandler upgradeUniverseHandler;

  public UniverseServerCertRotationReconciler(
      UpgradeUniverseHandler upgradeUniverseHandler,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory) {
    super(client, informerFactory, UniverseServerCertRotation.class, operatorUtils, namespace);
    this.upgradeUniverseHandler = upgradeUniverseHandler;
  }

  @Override
  protected void createActionReconcile(UniverseServerCertRotation rotation, Customer cust)
      throws Exception {
    String resourceName = rotation.getMetadata().getName();
    // Idempotency: once a task is tracked, NO_OP reconciles follow it (and its retries).
    if (isTerminal(rotation) || getCurrentTaskInfo(rotation) != null) {
      log.info(
          "Server cert rotation {} already has a task or has completed, skipping", resourceName);
      return;
    }
    try {
      String universeName = rotation.getSpec().getUniverse();
      YBUniverse ybUniverse =
          operatorUtils.getYBUniverse(
              new KubernetesResourceDetails(universeName, rotation.getMetadata().getNamespace()));
      Universe universe =
          ybUniverse == null
              ? null
              : OperatorUtils.getUniverseFromCr(cust.getId(), ybUniverse).orElse(null);
      if (universe == null) {
        updateStatus(
            rotation, STATE_FAILED, "Universe '" + universeName + "' not found", null, true, null);
        return;
      }

      UniverseDefinitionTaskParams details = universe.getUniverseDetails();
      // YBA rejects new operations while a failed placement-modifying task awaits its retry.
      if (details.updateInProgress || details.placementModificationTaskUuid != null) {
        log.debug(
            "Universe {} is busy or has a failed task pending retry, requeuing server cert rotation"
                + " {}",
            universeName,
            resourceName);
        workqueue.requeue(
            OperatorWorkQueue.getWorkQueueKey(rotation.getMetadata()),
            OperatorWorkQueue.ResourceAction.CREATE,
            false /* incrementRetry */);
        return;
      }

      String unsupportedReason = getUnsupportedReason(universe);
      if (unsupportedReason != null) {
        updateStatus(
            rotation,
            STATE_FAILED,
            "Universe '" + universeName + "' " + unsupportedReason,
            null,
            true,
            null);
        return;
      }

      log.info("Triggering server certificate rotation for universe {}", universeName);
      CertsRotateParams params = createServerCertRotateParams(rotation, ybUniverse, universe);
      UUID taskUUID = upgradeUniverseHandler.rotateCerts(params, cust, universe);
      log.info("Server cert rotation {} triggered with task: {}", resourceName, taskUUID);
      updateStatus(
          rotation,
          STATE_RUNNING,
          "Server certificate rotation task created",
          taskUUID,
          false,
          null);
    } catch (Exception e) {
      log.error("Failed to process create for server cert rotation {}", resourceName, e);
      updateStatus(
          rotation,
          STATE_FAILED,
          "Failed to trigger server certificate rotation: " + e.getMessage(),
          null,
          true,
          null);
    }
  }

  // Cert rotation is a one-shot operation; the spec is immutable so updates are no-ops.
  @Override
  protected void updateActionReconcile(UniverseServerCertRotation rotation, Customer cust)
      throws Exception {
    log.debug(
        "Update action not supported for server cert rotation {}",
        rotation.getMetadata().getName());
  }

  // NO_OP reconcile handler
  // Case 1: already Succeeded/Failed: nothing to do
  // Case 2: no task tracked yet: requeue CREATE
  // Case 3: a newer retry of the tracked task exists: track it instead
  // Case 4: task in an incomplete state: wait
  // Case 5: task succeeded: write a terminal Succeeded status
  // Case 6: task failed and is still the universe's pending placement task: the ybuniverse
  //  reconciler will retry it, record a non-terminal Retrying status
  // Case 7: task failed and nothing will retry it (e.g. superseded): write a terminal Failed status
  @Override
  protected void noOpActionReconcile(UniverseServerCertRotation rotation, Customer cust)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(rotation.getMetadata());
    String resourceName = rotation.getMetadata().getName();
    if (isTerminal(rotation)) {
      return;
    }

    TaskInfo taskInfo = getCurrentTaskInfo(rotation);
    if (taskInfo == null) {
      if (rotation.getStatus() != null && rotation.getStatus().getTaskUUID() != null) {
        updateStatus(
            rotation,
            STATE_FAILED,
            "Tracked task " + rotation.getStatus().getTaskUUID() + " no longer exists",
            null,
            true,
            null);
      } else {
        log.debug(
            "NoOp Action: server cert rotation {} not initialized, requeuing Create", resourceName);
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
      }
      return;
    }

    CustomerTask customerTask = CustomerTask.findByTaskUUID(taskInfo.getUuid());
    UUID universeUUID = customerTask != null ? customerTask.getTargetUUID() : null;
    List<TaskInfo> retries =
        universeUUID != null ? findRetries(taskInfo, universeUUID) : Collections.emptyList();
    TaskInfo retryTask =
        retries.stream().max(Comparator.comparing(TaskInfo::getCreateTime)).orElse(null);
    if (retryTask != null && !retryTask.getUuid().equals(taskInfo.getUuid())) {
      int retryCount = retries.size();
      log.info(
          "Server cert rotation {} now tracking retry task {} (retry {})",
          resourceName,
          retryTask.getUuid(),
          retryCount);
      updateStatus(
          rotation,
          STATE_RETRYING,
          "Server certificate rotation is being retried",
          retryTask.getUuid(),
          false,
          retryCount);
      taskInfo = retryTask;
    }

    if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
      log.debug("NoOp Action: server cert rotation {} task in progress", resourceName);
    } else if (taskInfo.getTaskState() == TaskInfo.State.Success) {
      log.info("Server cert rotation {} completed successfully", resourceName);
      updateStatus(
          rotation,
          STATE_SUCCEEDED,
          "Server certificate rotation completed",
          taskInfo.getUuid(),
          true,
          null);
      workqueue.resetRetries(mapKey);
    } else if (isPendingRetry(taskInfo, universeUUID)) {
      String message =
          errorMessage(
              "Server certificate rotation failed, waiting for the ybuniverse reconciler to retry"
                  + " it",
              taskInfo.getErrorMessage());
      // Written once: every status write triggers another NO_OP reconcile of this resource.
      if (!message.equals(rotation.getStatus().getMessage())) {
        updateStatus(rotation, STATE_RETRYING, message, taskInfo.getUuid(), false, null);
      }
    } else {
      log.warn("Server cert rotation {} failed and will not be retried", resourceName);
      updateStatus(
          rotation,
          STATE_FAILED,
          errorMessage("Server certificate rotation failed", taskInfo.getErrorMessage()),
          taskInfo.getUuid(),
          true,
          null);
      workqueue.resetRetries(mapKey);
    }
  }

  @Override
  protected void handleResourceDeletion(
      UniverseServerCertRotation rotation, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(rotation.getMetadata());
    log.info("Deleting server cert rotation: {}", rotation.getMetadata().getName());
    workqueue.clearState(mapKey);
  }

  // Returns why a server cert rotation cannot run on the universe, or null if it can.
  private static String getUnsupportedReason(Universe universe) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    UserIntent userIntent = details.getPrimaryCluster().userIntent;
    if (details.universePaused) {
      return "is paused";
    }
    if (!userIntent.enableNodeToNodeEncrypt && !userIntent.enableClientToNodeEncrypt) {
      return "does not have encryption in transit enabled";
    }
    return null;
  }

  private static CertsRotateParams createServerCertRotateParams(
      UniverseServerCertRotation rotation, YBUniverse ybUniverse, Universe universe)
      throws Exception {
    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    // rootCA and clientRootCA carry over unchanged, which is what makes this a server cert
    // rotation.
    CertsRotateParams params =
        mapper.readValue(mapper.writeValueAsString(details), CertsRotateParams.class);
    UserIntent userIntent = details.getPrimaryCluster().userIntent;
    // On Kubernetes, server and client certs must be rotated together for whichever TLS modes are
    // on, and rootCA always signs both.
    params.selfSignedServerCertRotate = userIntent.enableNodeToNodeEncrypt;
    params.selfSignedClientCertRotate = userIntent.enableClientToNodeEncrypt;
    params.rootAndClientRootCASame = true;
    if (rotation.getSpec().getUpgradeOption() != null) {
      YBUniverseReconciler.applyUpgradeOptions(
          params,
          ybUniverse,
          YBUniverseReconciler.toUpgradeOption(rotation.getSpec().getUpgradeOption().getValue()));
    } else {
      YBUniverseReconciler.applyUpgradeOptions(params, ybUniverse);
    }
    // Points task progress at the ybuniverse resource, as for operations it submits itself.
    params.setKubernetesResourceDetails(KubernetesResourceDetails.fromResource(ybUniverse));
    params.setUniverseUUID(universe.getUniverseUUID());
    return params;
  }

  // All retries in the given task's chain, i.e. cert rotation tasks on the universe that carry the
  // same originalTaskUUID, excluding the first task itself.
  private static List<TaskInfo> findRetries(TaskInfo taskInfo, UUID universeUUID) {
    UUID root = originalTaskUUID(taskInfo);
    Date since =
        TaskInfo.maybeGet(root).map(TaskInfo::getCreateTime).orElse(taskInfo.getCreateTime());
    return CustomerTask.findByTargetUUIDsAndTypesSince(
            Collections.singletonList(universeUUID),
            CustomerTask.TargetType.Universe,
            Collections.singletonList(CustomerTask.TaskType.CertsRotate),
            since)
        .stream()
        .map(ct -> TaskInfo.maybeGet(ct.getTaskUUID()))
        .flatMap(Optional::stream)
        .filter(t -> !t.getUuid().equals(root) && root.equals(originalTaskUUID(t)))
        .collect(Collectors.toList());
  }

  private static UUID originalTaskUUID(TaskInfo taskInfo) {
    JsonNode node = taskInfo.getTaskParams().get("originalTaskUUID");
    if (node == null || node.isNull() || node.asText().isEmpty()) {
      return taskInfo.getUuid();
    }
    return UUID.fromString(node.asText());
  }

  // A failed placement-modifying task stays recorded on the universe until a retry succeeds.
  private static boolean isPendingRetry(TaskInfo taskInfo, UUID universeUUID) {
    if (universeUUID == null) {
      return false;
    }
    return Universe.maybeGet(universeUUID)
        .map(u -> taskInfo.getUuid().equals(u.getUniverseDetails().placementModificationTaskUuid))
        .orElse(false);
  }

  private static boolean isTerminal(UniverseServerCertRotation rotation) {
    return rotation.getStatus() != null
        && (STATE_SUCCEEDED.equals(rotation.getStatus().getState())
            || STATE_FAILED.equals(rotation.getStatus().getState()));
  }

  private static String errorMessage(String prefix, String taskError) {
    if (taskError == null || taskError.isBlank()) {
      return prefix;
    }
    return prefix + ": " + taskError;
  }

  private TaskInfo getCurrentTaskInfo(UniverseServerCertRotation rotation) {
    if (rotation.getStatus() == null || rotation.getStatus().getTaskUUID() == null) {
      return null;
    }
    UUID taskUUID = UUID.fromString(rotation.getStatus().getTaskUUID());
    return TaskInfo.maybeGet(taskUUID).orElse(null);
  }

  private void updateStatus(
      UniverseServerCertRotation rotation,
      String state,
      String message,
      UUID taskUUID,
      boolean completed,
      Integer retryCount) {
    try {
      UniverseServerCertRotation latest =
          resourceClient
              .inNamespace(rotation.getMetadata().getNamespace())
              .withName(rotation.getMetadata().getName())
              .get();
      if (latest == null) {
        return;
      }
      UniverseServerCertRotationStatus status = latest.getStatus();
      if (status == null) {
        status = new UniverseServerCertRotationStatus();
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
      log.debug("Updated status for server cert rotation CR {}", latest.getMetadata().getName());
    } catch (Exception e) {
      log.error(
          "Failed to update cert rotation CR status for {}", rotation.getMetadata().getName(), e);
    }
  }
}
