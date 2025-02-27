// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.ScheduleTaskHelper;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue.ResourceAction;
import com.yugabyte.yw.common.utils.Pair;
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
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.BackupSchedule;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import play.libs.Json;

@Slf4j
public class ScheduledBackupReconciler extends AbstractReconciler<BackupSchedule>
    implements Runnable {

  private final SharedIndexInformer<BackupSchedule> scheduleInformer;
  private final SharedIndexInformer<StorageConfig> scInformer;
  private final MixedOperation<
          BackupSchedule, KubernetesResourceList<BackupSchedule>, Resource<BackupSchedule>>
      resourceClient;
  private final Lister<BackupSchedule> scheduleLister;
  private final BackupHelper backupHelper;
  private final ScheduleTaskHelper scheduleTaskHelper;
  private final ValidatingFormFactory formFactory;
  private final String namespace;
  private final OperatorUtils operatorUtils;
  private final OperatorWorkQueue workqueue;
  private final Map<String, UUID> scheduleTaskMap;

  public ScheduledBackupReconciler(
      BackupHelper backupHelper,
      ValidatingFormFactory formFactory,
      String namespace,
      OperatorUtils operatorUtils,
      KubernetesClient client,
      YBInformerFactory informerFactory,
      ScheduleTaskHelper scheduleTaskHelper) {
    super(client, informerFactory);
    this.scheduleInformer = informerFactory.getSharedIndexInformer(BackupSchedule.class, client);
    this.scInformer = informerFactory.getSharedIndexInformer(StorageConfig.class, client);
    this.resourceClient = client.resources(BackupSchedule.class);
    this.backupHelper = backupHelper;
    this.scheduleTaskHelper = scheduleTaskHelper;
    this.formFactory = formFactory;
    this.namespace = namespace;
    this.operatorUtils = operatorUtils;
    this.workqueue = new OperatorWorkQueue("BackupSchedule");
    this.scheduleInformer.addEventHandler(this);
    this.scheduleLister = new Lister<>(this.scheduleInformer.getIndexer());
    this.scheduleTaskMap = new HashMap<>();
  }

  @VisibleForTesting
  UUID getScheduleTaskMapValue(String key) {
    return this.scheduleTaskMap.getOrDefault(key, null);
  }

  @Override
  public void run() {
    log.info("Starting BackupSchedule reconciler thread");
    while (!Thread.currentThread().isInterrupted()) {
      if (scheduleInformer.hasSynced()) {
        break;
      }
    }
    while (true) {
      try {
        log.info("Trying to fetch BackupSchedule actions from workqueue...");
        if (workqueue.isEmpty()) {
          log.debug("Work Queue is empty");
        }
        Pair<String, OperatorWorkQueue.ResourceAction> pair = workqueue.pop();
        String key = pair.getFirst();
        OperatorWorkQueue.ResourceAction action = pair.getSecond();
        Objects.requireNonNull(key, "The workqueue item key can't be null.");
        log.debug("Found work item: {} {}", key, action);
        if ((!key.contains("/"))) {
          log.warn("Invalid resource key: {}", key);
          continue;
        }

        // Get the BackupSchedule resource's name
        // from workqueue key which is in format namespace/name/uid.
        // First get key namespace/name format
        String listerKey = OperatorWorkQueue.getListerKeyFromWorkQueueKey(key);
        BackupSchedule backupSchedule = scheduleLister.get(listerKey);
        log.info("Processing BackupSchedule name: {}, Object: {}", listerKey, backupSchedule);

        if (backupSchedule == null) {
          if (action == OperatorWorkQueue.ResourceAction.DELETE) {
            log.info(
                "Tried to delete BackupSchedule {} but it's no longer in Lister",
                backupSchedule.getMetadata().getName());
          }
          // Clear any state of the non-existing backup schedule from In-memory maps
          workqueue.clearState(key);
          continue;
        }
        if (backupSchedule
            .getMetadata()
            .getUid()
            .equals(OperatorWorkQueue.getResourceUidFromWorkQueueKey(key))) {
          reconcile(backupSchedule, action);
        } else {
          workqueue.clearState(key);
          log.debug(
              "Lister referencing older BackupSchedule with same name {}, ignoring", listerKey);
          continue;
        }

      } catch (Exception e) {
        log.error("Got Exception", e);
        try {
          Thread.sleep(reconcileExceptionBackoffMS);
          log.info("Continue BackupSchedule reconcile loop after failure backoff");
        } catch (InterruptedException e1) {
          log.info("caught interrupt, stopping reconciler", e);
          break;
        }
      }
    }
  }

  @Override
  public void onAdd(BackupSchedule backupSchedule) {
    enqueue(backupSchedule, OperatorWorkQueue.ResourceAction.CREATE);
  }

  @Override
  public void onUpdate(BackupSchedule backupScheduleOld, BackupSchedule backupScheduleNew) {
    if (backupScheduleNew.getMetadata().getDeletionTimestamp() != null) {
      enqueue(backupScheduleNew, OperatorWorkQueue.ResourceAction.DELETE);
      return;
    }
    // Treat this as no-op action enqueue
    enqueue(backupScheduleNew, OperatorWorkQueue.ResourceAction.NO_OP);
  }

  @Override
  public void onDelete(BackupSchedule backupSchedule, boolean b) {
    enqueue(backupSchedule, OperatorWorkQueue.ResourceAction.DELETE);
  }

  private void enqueue(BackupSchedule backupSchedule, OperatorWorkQueue.ResourceAction action) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    String listerName = OperatorWorkQueue.getListerKeyFromWorkQueueKey(mapKey);
    log.debug("Enqueue backupSchedule {} with action {}", listerName, action.toString());
    if (action.equals(OperatorWorkQueue.ResourceAction.NO_OP)) {
      workqueue.requeue(mapKey, action, false);
    } else {
      workqueue.add(new Pair<String, OperatorWorkQueue.ResourceAction>(mapKey, action));
      if (action.needsNoOpAction()) {
        workqueue.add(
            new Pair<String, OperatorWorkQueue.ResourceAction>(
                mapKey, OperatorWorkQueue.ResourceAction.NO_OP));
      }
    }
  }

  /**
   * Tries to achieve the desired state for backupSchedule.
   *
   * @param backupSchedule specified backupSchedule
   */
  protected void reconcile(BackupSchedule backupSchedule, OperatorWorkQueue.ResourceAction action) {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    String resourceName = backupSchedule.getMetadata().getName();
    String resourceNamespace = backupSchedule.getMetadata().getNamespace();
    log.info(
        "Reconcile for BackupSchedule metadata: Name = {}, Namespace = {}",
        resourceName,
        resourceNamespace);

    try {
      Customer cust = operatorUtils.getOperatorCustomer();
      // checking to see if the backup schedule was deleted.
      if (action == OperatorWorkQueue.ResourceAction.DELETE
          || backupSchedule.getMetadata().getDeletionTimestamp() != null) {
        handleBackupScheduleDeletion(backupSchedule, cust, action);
      } else if (action == OperatorWorkQueue.ResourceAction.CREATE) {
        createActionReconcile(backupSchedule, cust);
      } else if (action == OperatorWorkQueue.ResourceAction.UPDATE) {
        updateActionReconcile(backupSchedule, cust);
      } else if (action == OperatorWorkQueue.ResourceAction.NO_OP) {
        noOpActionReconcile(backupSchedule, cust);
      }
    } catch (Exception e) {
      log.error("Got Exception in Operator Action", e);
    }
  }

  // Delete the backup Schedule
  private void handleBackupScheduleDeletion(
      BackupSchedule backupSchedule, Customer cust, OperatorWorkQueue.ResourceAction action)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    UUID universeUUID =
        UUID.fromString(backupSchedule.getMetadata().getAnnotations().get("universeUUID"));
    Optional<Schedule> optSchedule =
        Schedule.maybeGetScheduleByUniverseWithName(
            operatorUtils.getYbaResourceName(backupSchedule.getMetadata()),
            universeUUID,
            cust.getUuid());
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
      enqueue(backupSchedule, ResourceAction.NO_OP);
    }
  }

  // Handles 3 cases
  // Case 1: Schedule is not present: Create with latest params
  // Case 2: Schedule is present but in error state: Update with latest params
  // Case 3: Schedule is present and Active and requires edit: Update
  //  with latest params
  private void createActionReconcile(BackupSchedule backupSchedule, Customer cust)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    String ybaScheduleName = OperatorUtils.getYbaResourceName(backupSchedule.getMetadata());
    String resourceName = backupSchedule.getMetadata().getName();
    String resourceNamespace = backupSchedule.getMetadata().getNamespace();
    BackupScheduleTaskParams scheduleParams = createScheduledBackupParams(backupSchedule, cust);
    Optional<Schedule> optionalSchedule =
        Schedule.maybeGetScheduleByUniverseWithName(
            scheduleParams.getScheduleParams().scheduleName,
            scheduleParams.getUniverseUUID(),
            scheduleParams.customerUUID);
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
  private void updateActionReconcile(BackupSchedule backupSchedule, Customer cust)
      throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    String ybaScheduleName = OperatorUtils.getYbaResourceName(backupSchedule.getMetadata());
    String resourceName = backupSchedule.getMetadata().getName();
    String resourceNamespace = backupSchedule.getMetadata().getNamespace();
    BackupScheduleTaskParams scheduleParams = createScheduledBackupParams(backupSchedule, cust);
    Optional<Schedule> optionalSchedule =
        Schedule.maybeGetScheduleByUniverseWithName(
            scheduleParams.getScheduleParams().scheduleName,
            scheduleParams.getUniverseUUID(),
            scheduleParams.customerUUID);
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
  private void noOpActionReconcile(BackupSchedule backupSchedule, Customer cust) throws Exception {
    String mapKey = OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata());
    String ybaScheduleName = OperatorUtils.getYbaResourceName(backupSchedule.getMetadata());
    String resourceName = backupSchedule.getMetadata().getName();
    String resourceNamespace = backupSchedule.getMetadata().getNamespace();
    BackupScheduleTaskParams scheduleParams = createScheduledBackupParams(backupSchedule, cust);
    Optional<Schedule> optionalSchedule =
        Schedule.maybeGetScheduleByUniverseWithName(
            scheduleParams.getScheduleParams().scheduleName,
            scheduleParams.getUniverseUUID(),
            scheduleParams.customerUUID);

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
