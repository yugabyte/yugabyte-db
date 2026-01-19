// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue.ResourceAction;
import com.yugabyte.yw.common.pitr.PitrConfigHelper;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.yugabyte.operator.v1alpha1.PitrConfig;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class PitrConfigReconciler extends AbstractReconciler<PitrConfig> {

  private final PitrConfigHelper pitrConfigHelper;
  private final ValidatingFormFactory formFactory;
  private final Map<String, UUID> pitrConfigTaskMap;

  public PitrConfigReconciler(
      PitrConfigHelper pitrConfigHelper,
      ValidatingFormFactory formFactory,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory) {
    super(client, informerFactory, PitrConfig.class, operatorUtils, namespace);
    this.pitrConfigHelper = pitrConfigHelper;
    this.formFactory = formFactory;
    this.pitrConfigTaskMap = new HashMap<>();
  }

  @VisibleForTesting
  UUID getPitrConfigTaskMapValue(String key) {
    return this.pitrConfigTaskMap.getOrDefault(key, null);
  }

  // Delete the PITR config
  @Override
  protected void handleResourceDeletion(
      PitrConfig pitrConfig, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(pitrConfig.getMetadata());
    UUID pitrConfigUUID = UUID.fromString(pitrConfig.getStatus().getResourceUUID());
    Optional<com.yugabyte.yw.models.PitrConfig> optPitrConfig =
        com.yugabyte.yw.models.PitrConfig.maybeGet(pitrConfigUUID);
    boolean pitrConfigRemoved = false;
    if (optPitrConfig.isPresent()) {
      // If owner universe is being deleted, delete PITR config from the
      // database, otherwise use delete task
      com.yugabyte.yw.models.PitrConfig pitrConfigModel = optPitrConfig.get();
      try {
        YBUniverse ybUniverse =
            operatorUtils.getYBUniverse(
                new KubernetesResourceDetails(
                    pitrConfig.getSpec().getUniverse(), pitrConfig.getMetadata().getNamespace()));
        if (ybUniverse == null || ybUniverse.getMetadata().getDeletionTimestamp() != null) {
          pitrConfigRemoved = pitrConfigModel.delete();
        } else {

          TaskInfo taskInfo = getCurrentTaskInfo(pitrConfig);
          if (taskInfo != null) {
            if (!TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())
                && taskInfo.getTaskState() != TaskInfo.State.Success) {
              pitrConfigTaskMap.remove(mapKey);
              // Retrying because the task failed
              workqueue.requeue(
                  mapKey, OperatorWorkQueue.ResourceAction.DELETE, true /* incrementRetry */);
              return;
            }
          } else {
            UUID taskUUID =
                pitrConfigHelper.deletePitrConfig(
                    cust.getUuid(),
                    pitrConfigModel.getUniverse().getUniverseUUID(),
                    pitrConfigModel.getUuid());
            if (taskUUID != null) {
              pitrConfigTaskMap.put(mapKey, taskUUID);
            }
          }
          workqueue.requeue(
              mapKey, OperatorWorkQueue.ResourceAction.DELETE, false /* incrementRetry */);
        }
      } catch (Exception e) {
        log.error("Failed to delete PITR config, will retry", e);
        return;
      }
    }

    if (pitrConfigRemoved || !optPitrConfig.isPresent()) {
      log.info(
          "Removing finalizer for PitrConfig {} in namespace {}",
          pitrConfig.getMetadata().getName(),
          pitrConfig.getMetadata().getNamespace());
      try {
        operatorUtils.removeFinalizer(pitrConfig, resourceClient);
        workqueue.clearState(mapKey);
      } catch (Exception e) {
        log.error("Removing finalizer failed, will retry", e);
      }
    }
  }

  // Handles 3 cases
  // Case 1: PITR config is not present: Create with latest params
  // Case 2: PITR config is present but in error state: Update with latest params
  // Case 3: PITR config is present and Active and requires edit: Update
  //  with latest params
  @Override
  protected void createActionReconcile(PitrConfig pitrConfig, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(pitrConfig.getMetadata());
    String resourceName = pitrConfig.getMetadata().getName();
    String resourceNamespace = pitrConfig.getMetadata().getNamespace();
    String universeName = pitrConfig.getSpec().getUniverse();
    boolean resourceExists =
        pitrConfig.getStatus() != null && pitrConfig.getStatus().getResourceUUID() != null;
    CreatePitrConfigParams pitrConfigParams =
        operatorUtils.getCreatePitrConfigParamsFromCr(pitrConfig);
    Universe universe =
        operatorUtils.getUniverseFromNameAndNamespace(
            cust.getId(), universeName, resourceNamespace);
    if (!resourceExists && !universe.universeIsLocked()) {
      log.info("Creating new PITR config {}", resourceName);
      createPitrConfigTask(pitrConfigParams, pitrConfig, cust, universe);
    } else {

      if (resourceExists) {
        UUID pitrConfigUuid = UUID.fromString(pitrConfig.getStatus().getResourceUUID());
        Optional<com.yugabyte.yw.models.PitrConfig> optionalPitrConfig =
            com.yugabyte.yw.models.PitrConfig.maybeGet(pitrConfigUuid);
        com.yugabyte.yw.models.PitrConfig pitrConfigModel = optionalPitrConfig.get();
        String state = null;
        if (pitrConfig.getStatus() != null && pitrConfig.getStatus().getMessage() != null) {
          String message = pitrConfig.getStatus().getMessage();
          if (message.contains("COMPLETE")) {
            state = "COMPLETE";
          } else if (message.contains("FAILED")) {
            state = "FAILED";
          }
        }
        log.debug("PitrConfig: {} current state: {}", resourceName, state);
        if ("FAILED".equals(state) && !universe.universeIsLocked()) {
          log.debug(
              "NoOp Action: PITR config {} in error state, retrying with latest params",
              resourceName);
          createUpdatePitrConfigTask(pitrConfig, cust, universe, pitrConfigModel);

        } else if ("COMPLETE".equals(state) && requiresEdit(pitrConfig, pitrConfigModel)) {
          createUpdatePitrConfigTask(pitrConfig, cust, universe, pitrConfigModel);
        }
      } else {
        // This means that universe is locked and PITR config model is not present
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
      }
    }
  }

  // Handles 2 cases
  // Case 1: PITR config is in error state: Creates edit task with latest params
  // Case 2: PITR config is Active and requires edit: Create edit task with latest params
  @Override
  protected void updateActionReconcile(PitrConfig pitrConfig, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(pitrConfig.getMetadata());
    String resourceName = pitrConfig.getMetadata().getName();
    String resourceNamespace = pitrConfig.getMetadata().getNamespace();
    UUID pitrConfigUuid = UUID.fromString(pitrConfig.getStatus().getResourceUUID());
    Optional<com.yugabyte.yw.models.PitrConfig> optionalPitrConfig =
        com.yugabyte.yw.models.PitrConfig.maybeGet(pitrConfigUuid);
    if (!optionalPitrConfig.isPresent()) {
      log.debug("PITR config does not exist, ignoring update");
      return;
    }
    com.yugabyte.yw.models.PitrConfig pitrConfigModel = optionalPitrConfig.get();
    Universe universe = pitrConfigModel.getUniverse();
    String state = null;
    if (pitrConfig.getStatus() != null && pitrConfig.getStatus().getMessage() != null) {
      String message = pitrConfig.getStatus().getMessage();
      if (message.contains("COMPLETE")) {
        state = "COMPLETE";
      } else if (message.contains("FAILED")) {
        state = "FAILED";
      }
    }
    log.debug("PitrConfig: {} current state: {}", resourceName, state);
    if ("FAILED".equals(state) && !universe.universeIsLocked()) {
      log.debug(
          "NoOp Action: PITR config {} in error state, retrying with latest params", resourceName);
      createUpdatePitrConfigTask(pitrConfig, cust, universe, pitrConfigModel);

    } else if ("COMPLETE".equals(state) && requiresEdit(pitrConfig, pitrConfigModel)) {
      createUpdatePitrConfigTask(pitrConfig, cust, universe, pitrConfigModel);
    }
  }

  // NO_OP reconcile handler
  // Case 1: Currently running task in incomplete state: NO_OP
  // Case 2: Currently running task in failed state: CREATE/UPDATE based on task type
  // Case 3: Currently running task in Success state: Reset retries and queue UPDATE if required
  // Case 4: No current task and Schedule not present: Requeue CREATE
  // Case 5: No current task and Schedule requires edit: Requeue UPDATE
  // Case 6: No current task and Schedule in error state: Requeue CREATE
  @Override
  protected void noOpActionReconcile(PitrConfig pitrConfig, Customer cust) throws Exception {

    String mapKey = OperatorWorkQueue.getWorkQueueKey(pitrConfig.getMetadata());
    String resourceName = pitrConfig.getMetadata().getName();
    boolean resourceExists =
        pitrConfig.getStatus() != null && pitrConfig.getStatus().getResourceUUID() != null;

    if (!resourceExists) {
      log.debug("NoOp Action: PITR Config {} not found, requeuing Create", resourceName);
      workqueue.requeue(
          mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
      return;
    }
    String resourceNamespace = pitrConfig.getMetadata().getNamespace();
    UUID pitrConfigUuid = UUID.fromString(pitrConfig.getStatus().getResourceUUID());
    Optional<com.yugabyte.yw.models.PitrConfig> optionalPitrConfig =
        com.yugabyte.yw.models.PitrConfig.maybeGet(pitrConfigUuid);

    TaskInfo taskInfo = getCurrentTaskInfo(pitrConfig);
    if (taskInfo != null) {
      if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
        log.debug("NoOp Action: PITR Config {} task in progress, requeuing no-op", resourceName);
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false /* incrementRetry */);
      } else if (taskInfo.getTaskState() != TaskInfo.State.Success) {
        if (taskInfo.getTaskType() == TaskType.CreatePitrConfig) {
          log.debug(
              "NoOp Action: PITR Config {} create task failed, requeuing Create", resourceName);
          workqueue.requeue(mapKey, ResourceAction.CREATE, true /* incrementRetry */);
        } else {
          log.debug(
              "NoOp Action: PITR Config {} update task failed, requeuing Update", resourceName);
          workqueue.requeue(mapKey, ResourceAction.UPDATE, true /* incrementRetry */);
        }
        pitrConfigTaskMap.remove(mapKey);
      } else {
        workqueue.resetRetries(mapKey);
        pitrConfigTaskMap.remove(mapKey);
        if (requiresEdit(pitrConfig, optionalPitrConfig.get())) {
          workqueue.requeue(mapKey, ResourceAction.UPDATE, false /* incrementRetry */);
        }
      }
    } else if (!optionalPitrConfig.isPresent()) {
      log.debug("NoOp Action: PITR Config {} creation failed, requeuing Create", resourceName);
      workqueue.requeue(
          mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
    } else {
      com.yugabyte.yw.models.PitrConfig pitrConfigModel = optionalPitrConfig.get();
      Universe universe = Universe.getOrBadRequest(pitrConfigModel.getUniverse().getUniverseUUID());
      String state = null;
      if (pitrConfig.getStatus() != null && pitrConfig.getStatus().getMessage() != null) {
        String message = pitrConfig.getStatus().getMessage();
        if (message.contains("COMPLETE")) {
          state = "COMPLETE";
        } else if (message.contains("FAILED")) {
          state = "FAILED";
        }
      }
      log.debug("PITR Config: {} current state: {}", pitrConfigModel.getName(), state);
      if ("COMPLETE".equals(state) && requiresEdit(pitrConfig, pitrConfigModel)) {
        workqueue.requeue(mapKey, ResourceAction.UPDATE, false /* incrementRetry */);
      } else if ("FAILED".equals(state)) {
        workqueue.requeue(mapKey, ResourceAction.CREATE, false /* incrementRetry */);
      } else {
        workqueue.resetRetries(mapKey);
      }
    }
  }

  private UUID createPitrConfigTask(
      CreatePitrConfigParams createPitrConfigParams,
      PitrConfig pitrConfig,
      Customer customer,
      Universe universe)
      throws Exception {
    try {
      ObjectMeta objectMeta = pitrConfig.getMetadata();
      if (CollectionUtils.isEmpty(objectMeta.getFinalizers())) {
        KubernetesResourceDetails universeResource =
            new KubernetesResourceDetails(
                pitrConfig.getSpec().getUniverse(), objectMeta.getNamespace());
        objectMeta.setOwnerReferences(
            Collections.singletonList(
                operatorUtils.getResourceOwnerReference(universeResource, YBUniverse.class)));
        objectMeta.setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
        Map<String, String> annotations = new HashMap<>();
        if (objectMeta.getAnnotations() != null) {
          annotations.putAll(annotations);
        }
        annotations.put("universeUUID", universe.getUniverseUUID().toString());
        objectMeta.setAnnotations(annotations);
        resourceClient
            .inNamespace(objectMeta.getNamespace())
            .withName(objectMeta.getName())
            .patch(pitrConfig);
      }
      UUID taskUUID =
          pitrConfigHelper.createPitrConfig(
              customer.getUuid(),
              universe.getUniverseUUID(),
              pitrConfig.getSpec().getTableType().toString(),
              pitrConfig.getSpec().getDatabase(),
              createPitrConfigParams);
      log.debug("PITR config creation triggered with task: {}", taskUUID);
      if (taskUUID != null) {
        pitrConfigTaskMap.put(
            OperatorWorkQueue.getWorkQueueKey(pitrConfig.getMetadata()), taskUUID);
      }
      return taskUUID;
    } catch (Exception e) {
      log.error("Failed to create PITR config: {} {}", pitrConfig.getMetadata().getName(), e);
      throw e;
    }
  }

  private UUID createUpdatePitrConfigTask(
      PitrConfig pitrConfig,
      Customer customer,
      Universe universe,
      com.yugabyte.yw.models.PitrConfig pitrConfigModel)
      throws Exception {
    UpdatePitrConfigParams updatePitrConfigParams =
        operatorUtils.getUpdatePitrConfigParamsFromCr(pitrConfig);
    UUID taskUUID =
        pitrConfigHelper.updatePitrConfig(
            customer.getUuid(),
            universe.getUniverseUUID(),
            pitrConfigModel.getUuid(),
            updatePitrConfigParams);
    log.debug("PITR config edit triggered with task: {}", taskUUID);
    if (taskUUID != null) {
      pitrConfigTaskMap.put(OperatorWorkQueue.getWorkQueueKey(pitrConfig.getMetadata()), taskUUID);
    }
    return taskUUID;
  }

  private boolean requiresEdit(
      PitrConfig pitrConfig, com.yugabyte.yw.models.PitrConfig pitrConfigModel) throws Exception {
    return pitrConfig.getSpec().getRetentionPeriodInSeconds()
            != pitrConfigModel.getRetentionPeriod()
        || pitrConfig.getSpec().getIntervalInSeconds() != pitrConfigModel.getScheduleInterval();
  }

  private TaskInfo getCurrentTaskInfo(PitrConfig pitrConfig) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(pitrConfig.getMetadata());
    UUID taskUUID = pitrConfigTaskMap.get(mapKey);
    if (taskUUID != null) {
      Optional<TaskInfo> optTaskInfo = TaskInfo.maybeGet(taskUUID);
      if (optTaskInfo.isPresent()) {
        return optTaskInfo.get();
      }
    }
    return null;
  }
}
