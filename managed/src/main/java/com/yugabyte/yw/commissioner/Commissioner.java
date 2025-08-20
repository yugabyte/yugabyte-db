// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableSet;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.TaskExecutionListener;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Consumer;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.MDC;
import play.inject.ApplicationLifecycle;
import play.libs.Json;

@Singleton
@Slf4j
public class Commissioner {

  public static final String TASK_ID = "commissioner_task_id";
  public static final String SUBTASK_ABORT_POSITION_PROPERTY = "subtask-abort-position";
  public static final String SUBTASK_PAUSE_POSITION_PROPERTY = "subtask-pause-position";
  public static final String YB_SOFTWARE_VERSION = "ybSoftwareVersion";
  public static final String YB_PREV_SOFTWARE_VERSION = "ybPrevSoftwareVersion";

  private final ExecutorService executor;

  private final TaskExecutor taskExecutor;

  // A map of task UUIDs to latches for currently paused tasks.
  private final Map<UUID, CountDownLatch> pauseLatches = new ConcurrentHashMap<>();

  private final ProviderEditRestrictionManager providerEditRestrictionManager;

  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public Commissioner(
      ApplicationLifecycle lifecycle,
      PlatformExecutorFactory platformExecutorFactory,
      TaskExecutor taskExecutor,
      ProviderEditRestrictionManager providerEditRestrictionManager,
      RuntimeConfGetter runtimeConfGetter) {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-%d").build();
    this.taskExecutor = taskExecutor;
    this.providerEditRestrictionManager = providerEditRestrictionManager;
    this.runtimeConfGetter = runtimeConfGetter;
    executor = platformExecutorFactory.createExecutor("commissioner", namedThreadFactory);
    log.info("Started Commissioner TaskPool.");
  }

  /**
   * Returns true if the task identified by the task type is abortable.
   *
   * @param taskType the task type.
   * @return true if abortable.
   */
  public boolean isTaskAbortable(TaskType taskType) {
    return TaskExecutor.isTaskAbortable(taskType.getTaskClass());
  }

  /**
   * Returns true if the task identified by the task type is retryable.
   *
   * @param taskType the task type.
   * @return true if retryable.
   */
  public boolean isTaskRetryable(TaskType taskType) {
    return TaskExecutor.isTaskRetryable(taskType.getTaskClass());
  }

  /**
   * Creates a new task runnable to run the required task, and submits it to the TaskExecutor.
   *
   * @param taskType the task type.
   * @param taskParams the task parameters.
   */
  public UUID submit(TaskType taskType, ITaskParams taskParams) {
    return submit(taskType, taskParams, null);
  }

  /**
   * Creates a new task runnable to run the required task, and submits it to the TaskExecutor.
   *
   * @param taskType the task type.
   * @param taskParams the task parameters.
   * @param taskUUID the task UUID
   */
  public UUID submit(TaskType taskType, ITaskParams taskParams, UUID taskUUID) {
    RunnableTask taskRunnable = null;
    try {
      if (runtimeConfGetter.getGlobalConf(
          GlobalConfKeys.enableTaskAndFailedRequestDetailedLogging)) {
        JsonNode taskParamsJson = Json.toJson(taskParams);
        JsonNode redactedJson =
            RedactingService.filterSecretFields(taskParamsJson, RedactionTarget.LOGS);
        log.debug(
            "Executing TaskType {} with params {}", taskType.toString(), redactedJson.toString());
      }
      // Create the task runnable object based on the various parameters passed in.
      taskRunnable = taskExecutor.createRunnableTask(taskType, taskParams, taskUUID);
      // Add the consumer to handle before task if available.
      taskRunnable.setTaskExecutionListener(getTaskExecutionListener());
      onTaskCreated(taskRunnable, taskParams);
      return taskExecutor.submit(taskRunnable, executor);
    } catch (Throwable t) {
      if (taskRunnable != null) {
        // Destroy the task initialization in case of failure.
        taskRunnable.getTask().terminate();
        taskRunnable.updateTaskDetailsOnError(TaskInfo.State.Failure, t);
      }

      String redactedTaskParams;
      try {
        JsonNode taskParamsJson = Json.toJson(taskParams);
        JsonNode redactedJson =
            RedactingService.filterSecretFields(taskParamsJson, RedactionTarget.LOGS);
        redactedTaskParams = redactedJson.toString();
      } catch (Exception jsonException) {
        String taskParamsString = taskParams.toString();
        redactedTaskParams = RedactingService.redactSensitiveInfoInString(taskParamsString);
        log.debug(
            "JSON serialization failed for task params, using string redaction: {}",
            jsonException.getMessage());
      }
      String msg = "Error processing " + taskType + " task for " + redactedTaskParams;
      log.error(msg, t);
      if (t instanceof PlatformServiceException) {
        throw t;
      }
      throw new RuntimeException(msg, t);
    }
  }

  private void onTaskCreated(RunnableTask taskRunnable, ITaskParams taskParams) {
    providerEditRestrictionManager.onTaskCreated(
        taskRunnable.getTaskUUID(), taskRunnable.getTask(), taskParams);
  }

  /**
   * Triggers task abort asynchronously. It can take some time for the task to abort. Caller can
   * check the task status for the final state.
   *
   * @param taskUUID the UUID of the task to be aborted.
   * @param force skip some checks like abortable if it is set.
   * @return true if the task is found running and abort is triggered successfully, else false.
   */
  public boolean abortTask(UUID taskUUID, boolean force) {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    if (!force && !isTaskAbortable(taskInfo.getTaskType())) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Invalid task type: Task %s cannot be aborted", taskUUID));
    }

    if (taskInfo.getTaskState() != TaskInfo.State.Running) {
      log.warn("Task {} is not running", taskUUID);
      return false;
    }
    CountDownLatch latch = pauseLatches.get(taskUUID);
    if (latch != null) {
      // Resume if it is already paused to abort faster.
      latch.countDown();
    }
    Optional<TaskInfo> optional = taskExecutor.abort(taskUUID, force);
    boolean success = optional.isPresent();
    if (success && BackupUtil.BACKUP_TASK_TYPES.contains(taskInfo.getTaskType())) {
      Backup.fetchAllBackupsByTaskUUID(taskUUID)
          .forEach((backup) -> backup.transitionState(BackupState.Stopping));
    }
    return success;
  }

  /**
   * Resumes a paused task. This is useful for fault injection to pause a task at a predefined
   * position (e.g 0) and get the list of subtasks to set the abort position during resume.
   *
   * @param taskUUID the UUID of the task to be resumed.
   * @return true if the task is found to be paused else false.
   */
  public boolean resumeTask(UUID taskUUID) {
    TaskInfo.getOrBadRequest(taskUUID);
    CountDownLatch latch = pauseLatches.get(taskUUID);
    if (latch == null) {
      return false;
    }
    Optional<RunnableTask> optional = taskExecutor.maybeGetRunnableTask(taskUUID);
    if (optional.isPresent()) {
      optional.get().setTaskExecutionListener(getTaskExecutionListener());
    }
    latch.countDown();
    // Wait for the task to come out of the wait and starts running.
    while (true) {
      try {
        CountDownLatch currentLatch = pauseLatches.get(taskUUID);
        if (currentLatch == null || currentLatch != latch) {
          break;
        }
        Thread.sleep(10);
      } catch (InterruptedException e) {
        throw new RuntimeException(e);
      }
    }
    return true;
  }

  public ObjectNode getStatusOrBadRequest(UUID taskUUID) {
    return mayGetStatus(taskUUID)
        .orElseThrow(
            () -> new PlatformServiceException(BAD_REQUEST, "Not able to find task " + taskUUID));
  }

  public Optional<ObjectNode> buildTaskStatus(
      CustomerTask task,
      TaskInfo taskInfo,
      Map<UUID, Set<String>> updatingTasks,
      Map<UUID, CustomerTask> lastTaskByTarget) {
    if (task == null || taskInfo == null) {
      return Optional.empty();
    }
    ObjectNode responseJson = Json.newObject();
    // Add some generic information about the task
    responseJson.put("title", task.getFriendlyDescription());
    responseJson.put("createTime", task.getCreateTime().toString());
    if (task.getCompletionTime() != null) {
      responseJson.put("completionTime", task.getCompletionTime().toString());
    }
    responseJson.put("target", task.getTargetName());
    responseJson.put("targetUUID", task.getTargetUUID().toString());
    responseJson.put("type", task.getType().name());
    // Find out the state of the task.
    responseJson.put("status", taskInfo.getTaskState().toString());
    // Get the percentage of subtasks that ran and completed
    responseJson.put("percent", taskInfo.getPercentCompleted());
    String correlationId = task.getCorrelationId();
    if (!Strings.isNullOrEmpty(correlationId)) {
      responseJson.put("correlationId", correlationId);
    }
    responseJson.put("userEmail", task.getUserEmail());

    // Get subtask groups and add other details to it if applicable.
    UserTaskDetails userTaskDetails;
    Optional<RunnableTask> optional = taskExecutor.maybeGetRunnableTask(taskInfo.getTaskUUID());
    if (optional.isPresent()) {
      userTaskDetails = taskInfo.getUserTaskDetails(optional.get().getTaskCache());
    } else {
      userTaskDetails = taskInfo.getUserTaskDetails();
    }
    ObjectNode details = Json.newObject();
    if (userTaskDetails != null && userTaskDetails.taskDetails != null) {
      details.set("taskDetails", Json.toJson(userTaskDetails.taskDetails));
    }
    ObjectNode versionNumbers = getVersionInfo(task, taskInfo);
    if (versionNumbers != null && !versionNumbers.isEmpty()) {
      details.set("versionNumbers", versionNumbers);
    }
    // Add auditLogConfig from the task details if it is present.
    // This info is useful to render the UI properly while task is in progress.
    if (taskInfo.getTaskParams().has("auditLogConfig")) {
      details.set("auditLogConfig", taskInfo.getTaskParams().get("auditLogConfig"));
    }
    responseJson.set("details", details);

    // Set abortable if eligible.
    responseJson.put("abortable", false);
    if (taskExecutor.isTaskRunning(task.getTaskUUID())) {
      // Task is abortable only when it is running.
      responseJson.put("abortable", isTaskAbortable(taskInfo.getTaskType()));
    }

    boolean retryable = false;
    // Set retryable if eligible.
    if (isTaskRetryable(taskInfo.getTaskType())
        && TaskInfo.ERROR_STATES.contains(taskInfo.getTaskState())) {
      if (task.getTargetType() == CustomerTask.TargetType.Provider) {
        CustomerTask lastTask = lastTaskByTarget.get(task.getTargetUUID());
        retryable = lastTask != null && lastTask.getTaskUUID().equals(task.getTaskUUID());
      } else {
        Set<String> taskUuidsToAllowRetry =
            updatingTasks.getOrDefault(task.getTargetUUID(), Collections.emptySet());
        retryable = taskUuidsToAllowRetry.contains(taskInfo.getTaskUUID().toString());
      }
    }
    responseJson.put("retryable", retryable);
    if (isTaskPaused(taskInfo.getTaskUUID())) {
      // Set this only if it is true. The thread is just parking. From the task state
      // perspective, it is still running.
      responseJson.put("paused", true);
    }
    return Optional.of(responseJson);
  }

  public ObjectNode getVersionInfo(CustomerTask task, TaskInfo taskInfo) {
    ObjectNode versionNumbers = Json.newObject();
    JsonNode taskParams = taskInfo.getTaskParams();
    if (ImmutableSet.of(
            CustomerTask.TaskType.SoftwareUpgrade,
            CustomerTask.TaskType.RollbackUpgrade,
            CustomerTask.TaskType.FinalizeUpgrade)
        .contains(task.getType())) {
      if (taskParams.has(Commissioner.YB_PREV_SOFTWARE_VERSION)) {
        versionNumbers.put(
            Commissioner.YB_PREV_SOFTWARE_VERSION,
            taskParams.get(Commissioner.YB_PREV_SOFTWARE_VERSION).asText());
      }

      if (taskParams.has(Commissioner.YB_SOFTWARE_VERSION)) {
        versionNumbers.put(
            Commissioner.YB_SOFTWARE_VERSION,
            taskParams.get(Commissioner.YB_SOFTWARE_VERSION).asText());
      }
    }
    return versionNumbers;
  }

  public boolean isTaskPaused(UUID taskUuid) {
    return pauseLatches.containsKey(taskUuid);
  }

  public boolean isTaskRunning(UUID taskUuid) {
    return taskExecutor.isTaskRunning(taskUuid);
  }

  public void waitForTask(UUID taskUuid) {
    taskExecutor.waitForTask(taskUuid);
  }

  public Optional<ObjectNode> mayGetStatus(UUID taskUUID) {
    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    if (task == null) {
      // We are not able to find the task. Report an error.
      log.error("Error fetching task progress for {}. Customer task is not found", taskUUID);
      return Optional.empty();
    }
    // Check if the task is in the DB.
    Optional<TaskInfo> optional = TaskInfo.maybeGet(taskUUID);
    if (!optional.isPresent()) {
      // We are not able to find the task. Report an error.
      log.error("Error fetching task progress for {}. TaskInfo is not found", taskUUID);
      return Optional.empty();
    }
    TaskInfo taskInfo = optional.get();
    Map<UUID, Set<String>> updatingTaskByTargetMap = new HashMap<>();
    Map<UUID, CustomerTask> lastTaskByTargetMap = new HashMap<>();
    Universe.getUniverseDetailsField(
            String.class,
            task.getTargetUUID(),
            UniverseDefinitionTaskParams.UPDATING_TASK_UUID_FIELD)
        .ifPresent(
            id ->
                updatingTaskByTargetMap
                    .computeIfAbsent(task.getTargetUUID(), uuid -> new HashSet<>())
                    .add(id));
    Universe.getUniverseDetailsField(
            String.class,
            task.getTargetUUID(),
            UniverseDefinitionTaskParams.PLACEMENT_MODIFICATION_TASK_UUID_FIELD)
        .ifPresent(
            id ->
                updatingTaskByTargetMap
                    .computeIfAbsent(task.getTargetUUID(), uuid -> new HashSet<>())
                    .add(id));
    lastTaskByTargetMap.put(
        task.getTargetUUID(), CustomerTask.getLastTaskByTargetUuid(task.getTargetUUID()));
    return buildTaskStatus(task, taskInfo, updatingTaskByTargetMap, lastTaskByTargetMap);
  }

  // Returns a map of target to updating task UUID.
  public Map<UUID, String> getUpdatingTaskUUIDsForTargets(Long customerId) {
    return Universe.getUniverseDetailsFields(
        String.class, customerId, UniverseDefinitionTaskParams.UPDATING_TASK_UUID_FIELD);
  }

  public Map<UUID, String> getPlacementModificationTaskUUIDsForTargets(Long customerId) {
    return Universe.getUniverseDetailsFields(
        String.class,
        customerId,
        UniverseDefinitionTaskParams.PLACEMENT_MODIFICATION_TASK_UUID_FIELD);
  }

  public JsonNode getTaskParams(UUID taskUUID) {
    Optional<TaskInfo> optional = TaskInfo.maybeGet(taskUUID);
    if (optional.isPresent()) {
      return optional.get().getTaskParams();
    }
    throw new PlatformServiceException(
        BAD_REQUEST, "Failed to retrieve task params for Task UUID: " + taskUUID);
  }

  private int getSubTaskPositionFromContext(String property) {
    int position = -1;
    String value = MDC.get(property);
    if (!Strings.isNullOrEmpty(value)) {
      try {
        position = Integer.parseInt(value);
      } catch (NumberFormatException e) {
        log.warn("Error in parsing subtask position for {}, ignoring it.", property, e);
        position = -1;
      }
    }
    return position;
  }

  // Returns the TaskExecutionListener instance.
  private TaskExecutionListener getTaskExecutionListener() {
    final Consumer<TaskInfo> beforeTaskConsumer = getBeforeTaskConsumer();
    DefaultTaskExecutionListener listener =
        new DefaultTaskExecutionListener(providerEditRestrictionManager, beforeTaskConsumer);
    return listener;
  }

  // Returns the composed for before task callback of TaskExecutionListener.
  private Consumer<TaskInfo> getBeforeTaskConsumer() {
    Consumer<TaskInfo> consumer = null;
    final int subTaskAbortPosition = getSubTaskPositionFromContext(SUBTASK_ABORT_POSITION_PROPERTY);
    final int subTaskPausePosition = getSubTaskPositionFromContext(SUBTASK_PAUSE_POSITION_PROPERTY);
    if (subTaskAbortPosition >= 0) {
      // Handle abort of subtask.
      consumer =
          taskInfo -> {
            if (taskInfo.getPosition() >= subTaskAbortPosition) {
              log.debug("Aborting task {} at position {}", taskInfo, taskInfo.getPosition());
              throw new CancellationException("Subtask cancelled");
            }
          };
    }
    if (subTaskPausePosition >= 0) {
      // Handle pause of subtask.
      Consumer<TaskInfo> pauseConsumer =
          taskInfo -> {
            if (taskInfo.getPosition() >= subTaskPausePosition) {
              log.debug("Pausing task {} at position {}", taskInfo, taskInfo.getPosition());
              final UUID parentTaskUUID = taskInfo.getParentUuid();
              try {
                // Insert if absent and get the latch.
                pauseLatches.computeIfAbsent(parentTaskUUID, k -> new CountDownLatch(1)).await();
              } catch (InterruptedException e) {
                throw new CancellationException("Subtask cancelled: " + e.getMessage());
              } finally {
                pauseLatches.remove(parentTaskUUID);
              }
              // Resume can set a new listener.
              RunnableTask runnableTask = taskExecutor.getRunnableTask(taskInfo.getParentUuid());
              TaskExecutionListener listener = runnableTask.getTaskExecutionListener();
              if (listener != null) {
                listener.beforeTask(taskInfo);
              }
            }
          };
      consumer = consumer == null ? pauseConsumer : consumer.andThen(pauseConsumer);
    }
    return consumer;
  }
}
