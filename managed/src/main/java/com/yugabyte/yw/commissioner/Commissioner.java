// Copyright (c) YugabyteDB, Inc.

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
import com.yugabyte.yw.commissioner.TaskExecutor.TaskParams;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.annotation.Transactional;
import java.time.Duration;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadFactory;
import java.util.function.Consumer;
import java.util.function.Predicate;
import javax.annotation.Nullable;
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

  private final TaskQueue taskQueue;

  // A map of task UUIDs to latches for currently paused tasks.
  private final Map<UUID, CountDownLatch> pauseLatches = new ConcurrentHashMap<>();

  private final ProviderEditRestrictionManager providerEditRestrictionManager;

  private final RuntimeConfGetter runtimeConfGetter;

  private final GFlagsValidation gFlagsValidation;

  @Inject
  public Commissioner(
      ApplicationLifecycle lifecycle,
      PlatformExecutorFactory platformExecutorFactory,
      TaskExecutor taskExecutor,
      TaskQueue taskQueue,
      ProviderEditRestrictionManager providerEditRestrictionManager,
      RuntimeConfGetter runtimeConfGetter,
      GFlagsValidation gFlagsValidation) {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-%d").build();
    this.taskExecutor = taskExecutor;
    this.taskQueue = taskQueue;
    this.providerEditRestrictionManager = providerEditRestrictionManager;
    this.runtimeConfGetter = runtimeConfGetter;
    this.gFlagsValidation = gFlagsValidation;
    this.executor = platformExecutorFactory.createExecutor("commissioner", namedThreadFactory);
    log.info("Started Commissioner TaskPool");
  }

  /**
   * Returns true if the task identified by the task type is abortable.
   *
   * @param taskType the task type.
   * @return true if abortable.
   */
  public static boolean isTaskTypeAbortable(TaskType taskType) {
    return TaskExecutor.isTaskAbortable(taskType.getTaskClass());
  }

  /**
   * Returns true if the task identified by the task type is retryable.
   *
   * @param taskType the task type.
   * @return true if retryable.
   */
  public static boolean isTaskTypeRetryable(TaskType taskType) {
    return TaskExecutor.isTaskRetryable(taskType.getTaskClass());
  }

  /**
   * Returns true if the task identified by the task type can rollback.
   *
   * @param taskType the task type.
   * @return true if can rollback.
   */
  public static boolean canTaskTypeRollback(TaskType taskType) {
    return TaskExecutor.canTaskRollback(taskType.getTaskClass());
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
    return submit(taskType, taskParams, taskUUID, null);
  }

  /**
   * Creates a new task runnable to run the required task, and submits it to the TaskExecutor.
   *
   * @param taskType the task type.
   * @param taskParams the task parameters.
   * @param taskUUID the task UUID
   * @param preTaskSubmitWork function to run before task submission
   */
  public UUID submit(
      TaskType taskType,
      ITaskParams taskParams,
      UUID taskUUID,
      @Nullable Consumer<RunnableTask> preTaskSubmitWork) {
    RunnableTask taskRunnable =
        taskQueue.enqueue(
            TaskParams.builder()
                .taskType(taskType)
                .taskParams(taskParams)
                .taskUuid(taskUUID)
                .build(),
            p -> {
              RunnableTask runnableTask = createRunnableTask(p, preTaskSubmitWork);
              runnableTask.setTaskExecutionListener(getTaskExecutionListener());
              return runnableTask;
            },
            (t, p) -> execute(t, p));
    return taskRunnable.getTaskUUID();
  }

  private void execute(RunnableTask taskRunnable, ITaskParams taskParams) {
    try {
      ITask task = taskRunnable.getTask();
      if (runtimeConfGetter.getGlobalConf(
          GlobalConfKeys.enableTaskAndFailedRequestDetailedLogging)) {
        JsonNode taskParamsJson = task.getTaskParams();
        JsonNode redactedJson =
            RedactingService.filterSecretFields(taskParamsJson, RedactionTarget.LOGS);
        log.debug(
            "Executing TaskType {} with params {}",
            taskRunnable.getTaskInfo().getTaskType(),
            redactedJson.toString());
      }
      onTaskCreated(taskRunnable, taskParams);
      taskExecutor.submit(taskRunnable, executor);
    } catch (Throwable t) {
      // Destroy the task initialization in case of failure.
      taskRunnable.getTask().terminate();
      taskRunnable.updateTaskDetailsOnError(State.Failure, t);

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

      String msg =
          String.format(
              "Error processing %s task for %s",
              taskRunnable.getTaskInfo().getTaskType(), redactedTaskParams);
      log.error(msg, t);
      if (t instanceof PlatformServiceException) {
        throw t;
      }
      throw new RuntimeException(msg, t);
    }
  }

  @Transactional
  private RunnableTask createRunnableTask(
      TaskParams creationParams, @Nullable Consumer<RunnableTask> preTaskSubmitWork) {
    // Create the task runnable object based on the various parameters passed in.
    RunnableTask taskRunnable = taskExecutor.createRunnableTask(creationParams);
    if (preTaskSubmitWork != null) {
      preTaskSubmitWork.accept(taskRunnable);
    }
    return taskRunnable;
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
    if (taskQueue.cancel(taskUUID)) {
      log.info("Task {} is removed from the queue", taskUUID);
      return true;
    }
    if (!force && !isTaskTypeAbortable(taskInfo.getTaskType())) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Invalid task type: Task %s cannot be aborted", taskUUID));
    }
    if (taskInfo.getTaskState() != TaskInfo.State.Running) {
      log.warn("Task {} is not in running state", taskUUID);
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
      List<TaskInfo> subTaskInfos,
      Map<UUID, Set<String>> updatingTasks,
      Map<UUID, CustomerTask> lastTaskByTarget) {
    if (task == null) {
      return Optional.empty();
    }
    TaskInfo taskInfo = task.getTaskInfo();
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
    Optional<RunnableTask> optional = taskExecutor.maybeGetRunnableTask(taskInfo.getUuid());
    userTaskDetails =
        taskInfo.getUserTaskDetails(
            subTaskInfos, optional.isPresent() ? optional.get().getTaskCache() : null);
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
    // Add queryLogConfig from the task details if it is present.
    if (taskInfo.getTaskParams().has("queryLogConfig")) {
      details.set("queryLogConfig", taskInfo.getTaskParams().get("queryLogConfig"));
    }
    // Add metricsExportConfig from the task details if it is present.
    if (taskInfo.getTaskParams().has("metricsExportConfig")) {
      details.set("metricsExportConfig", taskInfo.getTaskParams().get("metricsExportConfig"));
    }

    responseJson.set("details", details);

    // Set abortable if eligible.
    responseJson.put("abortable", isTaskAbortable(taskInfo));
    // Set retryable if eligible.
    boolean retryable =
        isTaskRetryable(
            taskInfo,
            tf -> {
              if (task.getTargetType() == CustomerTask.TargetType.Provider) {
                CustomerTask lastTask = lastTaskByTarget.get(task.getTargetUUID());
                return lastTask != null && lastTask.getTaskUUID().equals(task.getTaskUUID());
              }

              JsonNode xClusterConfigNode = taskInfo.getTaskParams().get("xClusterConfig");
              if (xClusterConfigNode != null && !xClusterConfigNode.isNull()) {
                XClusterConfig xClusterConfig =
                    Json.fromJson(xClusterConfigNode, XClusterConfig.class);
                return CustomerTaskManager.isXClusterTaskRetryable(tf.getUuid(), xClusterConfig);
              }

              Set<String> taskUuidsToAllowRetry =
                  updatingTasks.getOrDefault(task.getTargetUUID(), Collections.emptySet());
              return taskUuidsToAllowRetry.contains(taskInfo.getUuid().toString());
            });
    responseJson.put("retryable", retryable);
    responseJson.put("canRollback", canTaskRollback(taskInfo));
    if (isTaskPaused(taskInfo.getUuid())) {
      // Set this only if it is true. The thread is just parking. From the task state
      // perspective, it is still running.
      responseJson.put("paused", true);
    }
    return Optional.of(responseJson);
  }

  public boolean isTaskAbortable(TaskInfo taskInfo) {
    RunnableTask taskRunnable = taskQueue.find(taskInfo.getUuid());
    if (taskRunnable == null || taskRunnable.isRunning()) {
      // Check with the executor if the task is not queued or already running.
      return isTaskTypeAbortable(taskInfo.getTaskType())
          && taskExecutor.isTaskRunning(taskInfo.getUuid());
    }
    // Task is still in the queue.
    return true;
  }

  public boolean isTaskRetryable(TaskInfo taskInfo, Predicate<TaskInfo> moreCondition) {
    if (isTaskTypeRetryable(taskInfo.getTaskType())
        && TaskInfo.ERROR_STATES.contains(taskInfo.getTaskState())) {
      return moreCondition.test(taskInfo);
    }
    return false;
  }

  public boolean canTaskRollback(TaskInfo taskInfo) {
    return canTaskTypeRollback(taskInfo.getTaskType())
        && TaskInfo.ERROR_STATES.contains(taskInfo.getTaskState());
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

  public void waitForTask(UUID taskUuid, @Nullable Duration timeout) {
    taskExecutor.waitForTask(taskUuid, timeout);
  }

  public Optional<ObjectNode> mayGetStatus(UUID taskUUID) {
    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    if (task == null) {
      // We are not able to find the task. Report an error.
      log.error("Customer task with task UUID {} is not found", taskUUID);
      return Optional.empty();
    }
    TaskInfo taskInfo = task.getTaskInfo();
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
    return buildTaskStatus(
        task, taskInfo.getSubTasks(), updatingTaskByTargetMap, lastTaskByTargetMap);
  }

  // Returns a map of target to updating task UUID.
  public Map<UUID, String> getUpdatingTaskUUIDsForTargets(
      Long customerId, @Nullable UUID specificTargetUuid) {
    return Universe.getUniverseDetailsFields(
        String.class,
        customerId,
        UniverseDefinitionTaskParams.UPDATING_TASK_UUID_FIELD,
        specificTargetUuid);
  }

  public Map<UUID, String> getPlacementModificationTaskUUIDsForTargets(
      Long customerId, @Nullable UUID specificTargetUuid) {
    return Universe.getUniverseDetailsFields(
        String.class,
        customerId,
        UniverseDefinitionTaskParams.PLACEMENT_MODIFICATION_TASK_UUID_FIELD,
        specificTargetUuid);
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
    final Consumer<TaskInfo> afterTaskConsumer = getAfterTaskConsumer();
    DefaultTaskExecutionListener listener =
        new DefaultTaskExecutionListener(beforeTaskConsumer, afterTaskConsumer);
    return listener;
  }

  // Returns the composed for before task callback of TaskExecutionListener.
  protected Consumer<TaskInfo> getBeforeTaskConsumer() {
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

  protected Consumer<TaskInfo> getAfterTaskConsumer() {
    return taskInfo -> {
      if (taskInfo.getParentUuid() == null) {
        log.debug("Parent task {} has completed", taskInfo.getTaskType());
        try {
          taskQueue.dequeue(
              taskExecutor.getRunnableTask(taskInfo.getUuid()), (t, p) -> execute(t, p));
        } catch (Exception e) {
          log.error("Error occurred in running the next task - {}", e.getMessage());
        }
      }
      providerEditRestrictionManager.onTaskFinished(taskInfo.getUuid());
    };
  }
}
