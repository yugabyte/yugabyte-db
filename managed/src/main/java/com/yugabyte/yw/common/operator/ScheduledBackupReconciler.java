// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.ScheduleTaskHelper;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue.ResourceAction;
import com.yugabyte.yw.common.operator.utils.ResourceAnnotationKeys;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleEditParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Model;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.BackupSchedule;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import play.libs.Json;

@Slf4j
public class ScheduledBackupReconciler extends AbstractReconciler<BackupSchedule> {

  private final SharedIndexInformer<StorageConfig> scInformer;
  private final BackupHelper backupHelper;
  private final ScheduleTaskHelper scheduleTaskHelper;
  private final ValidatingFormFactory formFactory;
  private final Map<String, UUID> scheduleTaskMap;

  public ScheduledBackupReconciler(
      BackupHelper backupHelper,
      ValidatingFormFactory formFactory,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory,
      ScheduleTaskHelper scheduleTaskHelper) {
    super(client, informerFactory, BackupSchedule.class, operatorUtils, namespace);
    this.scInformer = informerFactory.getSharedIndexInformer(StorageConfig.class, client);
    this.backupHelper = backupHelper;
    this.scheduleTaskHelper = scheduleTaskHelper;
    this.formFactory = formFactory;
    this.scheduleTaskMap = new HashMap<>();
  }

  @VisibleForTesting
  UUID getScheduleTaskMapValue(String key) {
    return this.scheduleTaskMap.getOrDefault(key, null);
  }

  // Delete the backup Schedule
  @Override
  protected void handleResourceDeletion(
      BackupSchedule backupSchedule, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    UUID universeUUID =
        UUID.fromString(backupSchedule.getMetadata().getAnnotations().get("universeUUID"));
    String scheduleName = operatorUtils.getYbaResourceName(backupSchedule.getMetadata());
    if (backupSchedule.getSpec().getName() != null) {
      scheduleName = OperatorUtils.kubernetesCompatName(backupSchedule.getSpec().getName());
    }
    Optional<Schedule> optSchedule;
    if (backupSchedule.getMetadata().getAnnotations() != null
        && backupSchedule
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      optSchedule =
          Schedule.maybeGet(
              cust.getUuid(),
              UUID.fromString(
                  backupSchedule
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      optSchedule =
          Schedule.maybeGetScheduleByUniverseWithName(scheduleName, universeUUID, cust.getUuid());
    }
    boolean requeueNoOp = (action == ResourceAction.NO_OP);
    boolean scheduleRemoved = false;
    if (optSchedule.isPresent()) {
      // If owner universe is being deleted, just delete schedule object
      // otherwise use delete task
      try {
        YBUniverse ybUniverse =
            operatorUtils.getYBUniverse(
                new KubernetesResourceDetails(
                    backupSchedule.getSpec().getUniverse(),
                    backupSchedule.getMetadata().getNamespace()));
        if (ybUniverse == null || ybUniverse.getMetadata().getDeletionTimestamp() != null) {
          ScheduleTask.getAllTasks(optSchedule.get().getScheduleUUID()).forEach(Model::delete);
          scheduleRemoved = optSchedule.get().delete();
        } else {
          UUID taskUUID =
              scheduleTaskHelper.createDeleteScheduledBackupTask(
                  optSchedule.get(), universeUUID, cust);
        }
      } catch (Exception e) {
        log.error("Failed to delete schedule task, will retry", e);
        return;
      }
    }
    if (scheduleRemoved || !optSchedule.isPresent()) {
      log.info(
          "Removing finalizer for BackupSchedule {} in namespace {}",
          backupSchedule.getMetadata().getName(),
          backupSchedule.getMetadata().getNamespace());
      backupSchedule.getMetadata().setFinalizers(Collections.emptyList());
      try {
        operatorUtils.removeFinalizer(backupSchedule, resourceClient);
        requeueNoOp = false;
        workqueue.clearState(mapKey);
      } catch (Exception e) {
        log.error("Removing finalizer failed, will retry", e);
      }
    }
    if (requeueNoOp) {
      workqueue.requeue(mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false /* incrementRetry */);
    }
  }

  // Handles 3 cases
  // Case 1: Schedule is not present: Create with latest params
  // Case 2: Schedule is present but in error state: Update with latest params
  // Case 3: Schedule is present and Active and requires edit: Update
  //  with latest params
  @Override
  protected void createActionReconcile(BackupSchedule backupSchedule, Customer cust)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    String ybaScheduleName = OperatorUtils.getYbaResourceName(backupSchedule.getMetadata());
    String resourceName = backupSchedule.getMetadata().getName();
    String resourceNamespace = backupSchedule.getMetadata().getNamespace();
    BackupScheduleTaskParams scheduleParams = createScheduledBackupParams(backupSchedule, cust);
    Optional<Schedule> optionalSchedule;
    if (backupSchedule.getMetadata().getAnnotations() != null
        && backupSchedule
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      optionalSchedule =
          Schedule.maybeGet(
              cust.getUuid(),
              UUID.fromString(
                  backupSchedule
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      optionalSchedule =
          Schedule.maybeGetScheduleByUniverseWithName(
              scheduleParams.getScheduleParams().scheduleName,
              scheduleParams.getUniverseUUID(),
              scheduleParams.customerUUID);
    }
    Universe universe =
        Universe.getOrBadRequest(scheduleParams.getScheduleParams().getUniverseUUID(), cust);
    if (!optionalSchedule.isPresent() && !universe.universeIsLocked()) {
      log.info("Creating new backupSchedule {}", ybaScheduleName);
      createScheduleTask(scheduleParams, backupSchedule, cust, universe);
    } else {
      Schedule schedule = optionalSchedule.get();
      log.debug("Schedule: {} current state: {}", schedule.getScheduleName(), schedule.getStatus());
      if (schedule.getStatus() == Schedule.State.Error && !universe.universeIsLocked()) {
        // TODO(Vivek): Improve on this once we have the "prevState" of schedule available
        ScheduleTask lastTask = ScheduleTask.getLastTask(schedule.getScheduleUUID());
        log.debug(
            "NoOp Action: Backup Schedule {} in error state, retrying with latest params",
            ybaScheduleName);
        if (lastTask == null) {
          // Create with latest params
          optionalSchedule.get().delete();
          createScheduleTask(scheduleParams, backupSchedule, cust, universe);
        } else {
          createEditScheduleTask(backupSchedule, cust, universe, schedule);
        }
      } else if (schedule.getStatus() == Schedule.State.Active
          && requiresEdit(backupSchedule, schedule)) {
        createEditScheduleTask(backupSchedule, cust, universe, schedule);
      }
    }
  }

  // Handles 2 cases
  // Case 1: Schedule is in error state: Creates edit task with latest params
  // Case 2: Schedule is Active and requires edit: Create edit task with latest params
  @Override
  protected void updateActionReconcile(BackupSchedule backupSchedule, Customer cust)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    String ybaScheduleName = OperatorUtils.getYbaResourceName(backupSchedule.getMetadata());
    String resourceName = backupSchedule.getMetadata().getName();
    String resourceNamespace = backupSchedule.getMetadata().getNamespace();
    BackupScheduleTaskParams scheduleParams = createScheduledBackupParams(backupSchedule, cust);
    Optional<Schedule> optionalSchedule;
    if (backupSchedule.getMetadata().getAnnotations() != null
        && backupSchedule
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      optionalSchedule =
          Schedule.maybeGet(
              cust.getUuid(),
              UUID.fromString(
                  backupSchedule
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      optionalSchedule =
          Schedule.maybeGetScheduleByUniverseWithName(
              scheduleParams.getScheduleParams().scheduleName,
              scheduleParams.getUniverseUUID(),
              scheduleParams.customerUUID);
    }
    if (!optionalSchedule.isPresent()) {
      log.debug("Schedule does not exist, ignoring update");
      return;
    }
    Universe universe =
        Universe.getOrBadRequest(scheduleParams.getScheduleParams().getUniverseUUID());
    Schedule schedule = optionalSchedule.get();
    if (schedule.getStatus() == Schedule.State.Error) {
      log.info(
          "Backup Schedule {} in error state, re-trying with latest params",
          schedule.getScheduleName());
      createEditScheduleTask(backupSchedule, cust, universe, schedule);
    } else if (schedule.getStatus() == Schedule.State.Active
        && requiresEdit(backupSchedule, schedule)) {
      createEditScheduleTask(backupSchedule, cust, universe, schedule);
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
  protected void noOpActionReconcile(BackupSchedule backupSchedule, Customer cust)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    String ybaScheduleName = OperatorUtils.getYbaResourceName(backupSchedule.getMetadata());
    String resourceName = backupSchedule.getMetadata().getName();
    String resourceNamespace = backupSchedule.getMetadata().getNamespace();
    BackupScheduleTaskParams scheduleParams = createScheduledBackupParams(backupSchedule, cust);
    Optional<Schedule> optionalSchedule;
    if (backupSchedule.getMetadata().getAnnotations() != null
        && backupSchedule
            .getMetadata()
            .getAnnotations()
            .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
      optionalSchedule =
          Schedule.maybeGet(
              cust.getUuid(),
              UUID.fromString(
                  backupSchedule
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
    } else {
      optionalSchedule =
          Schedule.maybeGetScheduleByUniverseWithName(
              scheduleParams.getScheduleParams().scheduleName,
              scheduleParams.getUniverseUUID(),
              scheduleParams.customerUUID);
    }

    TaskInfo taskInfo = getCurrentTaskInfo(backupSchedule);
    if (taskInfo != null) {
      if (TaskInfo.INCOMPLETE_STATES.contains(taskInfo.getTaskState())) {
        log.debug(
            "NoOp Action: Backup Schedule {} task in progress, requeuing no-op", ybaScheduleName);
        workqueue.requeue(
            mapKey, OperatorWorkQueue.ResourceAction.NO_OP, false /* incrementRetry */);
      } else if (taskInfo.getTaskState() != TaskInfo.State.Success) {
        if (taskInfo.getTaskType() == TaskType.CreateBackupScheduleKubernetes) {
          log.debug(
              "NoOp Action: Backup Schedule {} create task failed, requeuing Create",
              ybaScheduleName);
          workqueue.requeue(mapKey, ResourceAction.CREATE, true /* incrementRetry */);
        } else {
          log.debug(
              "NoOp Action: Backup Schedule {} update task failed, requeuing Update",
              ybaScheduleName);
          workqueue.requeue(mapKey, ResourceAction.UPDATE, true /* incrementRetry */);
        }
        scheduleTaskMap.remove(mapKey);
      } else {
        workqueue.resetRetries(mapKey);
        scheduleTaskMap.remove(mapKey);
        if (requiresEdit(backupSchedule, optionalSchedule.get())) {
          workqueue.requeue(mapKey, ResourceAction.UPDATE, false /* incrementRetry */);
        }
      }
    } else if (!optionalSchedule.isPresent()) {
      log.debug(
          "NoOp Action: Backup Schedule {} creation failed, requeuing Create", ybaScheduleName);
      workqueue.requeue(
          mapKey, OperatorWorkQueue.ResourceAction.CREATE, false /* incrementRetry */);
    } else {
      Schedule schedule = optionalSchedule.get();
      Universe universe = Universe.getOrBadRequest(schedule.getOwnerUUID());
      log.debug("Schedule: {} current state: {}", schedule.getScheduleName(), schedule.getStatus());
      if (schedule.getStatus() == Schedule.State.Active && requiresEdit(backupSchedule, schedule)) {
        workqueue.requeue(mapKey, ResourceAction.UPDATE, false /* incrementRetry */);
      } else if (schedule.getStatus() == Schedule.State.Error) {
        workqueue.requeue(mapKey, ResourceAction.CREATE, false /* incrementRetry */);
      } else {
        workqueue.resetRetries(mapKey);
      }
    }
  }

  // Helper methods
  private BackupScheduleTaskParams createScheduledBackupParams(
      BackupSchedule backupSchedule, Customer cust) throws Exception {
    BackupRequestParams backupRequestParams =
        operatorUtils.getScheduleBackupRequestFromCr(backupSchedule, scInformer);

    Universe universe = Universe.getOrBadRequest(backupRequestParams.getUniverseUUID());
    ObjectNode universeDetailsNode = Json.mapper().valueToTree(universe.getUniverseDetails());
    BackupScheduleTaskParams params =
        Json.mapper().treeToValue(universeDetailsNode, BackupScheduleTaskParams.class);
    params.setScheduleParams(backupRequestParams);
    params.setUniverseUUID(universe.getUniverseUUID());
    params.setCustomerUUID(cust.getUuid());
    return params;
  }

  private BackupScheduleTaskParams createScheduledBackupEditParams(
      BackupSchedule backupSchedule, Customer cust, Schedule schedule, Universe universe)
      throws Exception {
    BackupRequestParams newBackupRequestParams =
        operatorUtils.getScheduleBackupRequestFromCr(backupSchedule, scInformer);
    BackupScheduleEditParams editParams = new BackupScheduleEditParams(newBackupRequestParams);
    BackupRequestParams currentBackupRequestParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    currentBackupRequestParams.applyScheduleEditParams(editParams);

    ObjectNode universeDetailsNode = Json.mapper().valueToTree(universe.getUniverseDetails());
    BackupScheduleTaskParams params =
        Json.mapper().treeToValue(universeDetailsNode, BackupScheduleTaskParams.class);
    params.setScheduleParams(currentBackupRequestParams);
    params.setCustomerUUID(cust.getUuid());
    params.setScheduleUUID(schedule.getScheduleUUID());
    return params;
  }

  @VisibleForTesting
  UUID createScheduleTask(
      BackupScheduleTaskParams taskParams,
      BackupSchedule backupSchedule,
      Customer customer,
      Universe universe)
      throws Exception {
    try {
      ObjectMeta objectMeta = backupSchedule.getMetadata();
      if (CollectionUtils.isEmpty(objectMeta.getFinalizers())) {
        KubernetesResourceDetails universeResource =
            new KubernetesResourceDetails(
                backupSchedule.getSpec().getUniverse(), objectMeta.getNamespace());
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
            .patch(backupSchedule);
      }
      UUID taskUUID =
          scheduleTaskHelper.createCreateScheduledBackupTask(taskParams, customer, universe);
      log.debug("Backup schedule creation triggered with task: {}", taskUUID);
      if (taskUUID != null) {
        scheduleTaskMap.put(
            OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata()), taskUUID);
      }
      return taskUUID;
    } catch (Exception e) {
      log.error(
          "Failed to create backup schedule: {} {}", backupSchedule.getMetadata().getName(), e);
      throw e;
    }
  }

  private UUID createEditScheduleTask(
      BackupSchedule backupSchedule, Customer customer, Universe universe, Schedule schedule)
      throws Exception {
    BackupScheduleTaskParams taskParams =
        createScheduledBackupEditParams(backupSchedule, customer, schedule, universe);
    UUID taskUUID =
        scheduleTaskHelper.createEditScheduledBackupTask(taskParams, customer, universe, schedule);
    log.debug("Backup schedule edit triggered with task: {}", taskUUID);
    if (taskUUID != null) {
      scheduleTaskMap.put(
          OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata()), taskUUID);
    }
    return taskUUID;
  }

  private boolean requiresEdit(BackupSchedule backupSchedule, Schedule schedule) throws Exception {
    BackupRequestParams newBackupRequestParams =
        operatorUtils.getScheduleBackupRequestFromCr(backupSchedule, scInformer);
    BackupRequestParams currentBackupRequestParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    return currentBackupRequestParams.compareScheduleParams(newBackupRequestParams);
  }

  private TaskInfo getCurrentTaskInfo(BackupSchedule backupSchedule) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    UUID taskUUID = scheduleTaskMap.get(mapKey);
    if (taskUUID != null) {
      Optional<TaskInfo> optTaskInfo = TaskInfo.maybeGet(taskUUID);
      if (optTaskInfo.isPresent()) {
        return optTaskInfo.get();
      }
    }
    return null;
  }
}
