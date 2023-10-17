// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static play.mvc.Http.Status.BAD_REQUEST;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Strings;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.TaskExecutionListener;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import play.inject.ApplicationLifecycle;
import play.libs.Json;
import scala.concurrent.ExecutionContext;

@Singleton
@Slf4j
public class Commissioner {

  public static final String SUBTASK_ABORT_POSITION_PROPERTY = "subtask-abort-position";
  public static final String SUBTASK_PAUSE_POSITION_PROPERTY = "subtask-pause-position";
  public static final String ENABLE_DETAILED_LOGGING_FAILED_REQUEST =
      "yb.logging.enable_task_failed_request_logs";

  private final ExecutorService executor;

  private final TaskExecutor taskExecutor;
  private final RuntimeConfigFactory runtimeConfigFactory;

  // A map of all task UUID's to the task runnable objects for all the user tasks that are currently
  // active. Recently completed tasks are also in this list, their completion percentage should be
  // persisted before removing the task from this map.
  private final Map<UUID, RunnableTask> runningTasks = new ConcurrentHashMap<>();

  // A map of task UUIDs to latches for currently paused tasks.
  private final Map<UUID, CountDownLatch> pauseLatches = new ConcurrentHashMap<>();

  @Inject
  public Commissioner(
      ProgressMonitor progressMonitor,
      ApplicationLifecycle lifecycle,
      PlatformExecutorFactory platformExecutorFactory,
      RuntimeConfigFactory runConfigFactory,
      TaskExecutor taskExecutor) {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-%d").build();
    this.taskExecutor = taskExecutor;
    this.runtimeConfigFactory = runConfigFactory;
    executor = platformExecutorFactory.createExecutor("commissioner", namedThreadFactory);
    log.info("Started Commissioner TaskPool.");
    progressMonitor.start(runningTasks);
    log.info("Started TaskProgressMonitor thread.");
  }

  /**
   * Returns true if the task identified by the task type is abortable.
   *
   * @param taskType the task type.
   * @return true if abortable.
   */
  public static boolean isTaskAbortable(TaskType taskType) {
    return TaskExecutor.isTaskAbortable(TaskExecutor.getTaskClass(taskType));
  }

  /**
   * Returns true if the task identified by the task type is retryable.
   *
   * @param taskType the task type.
   * @return true if retryable.
   */
  public static boolean isTaskRetryable(TaskType taskType) {
    return TaskExecutor.isTaskRetryable(TaskExecutor.getTaskClass(taskType));
  }

  /**
   * Creates a new task runnable to run the required task, and submits it to the TaskExecutor.
   *
   * @param taskType the task type.
   * @param taskParams the task parameters.
   * @return
   */
  public UUID submit(TaskType taskType, ITaskParams taskParams) {
    RunnableTask taskRunnable = null;
    try {
      if (runtimeConfigFactory
          .globalRuntimeConf()
          .getBoolean(ENABLE_DETAILED_LOGGING_FAILED_REQUEST)) {
        JsonNode redactedJson =
            RedactingService.filterSecretFields(Json.toJson(taskParams), RedactionTarget.LOGS);
        log.debug("Executing TaskType {} with params {}", taskType, redactedJson);
      }
      // Create the task runnable object based on the various parameters passed in.
      taskRunnable = taskExecutor.createRunnableTask(taskType, taskParams);
      // Add the consumer to handle before task if available.
      taskRunnable.setTaskExecutionListener(getTaskExecutionListener());
      UUID taskUUID = taskExecutor.submit(taskRunnable, executor);
      // Add this task to our queue.
      runningTasks.put(taskUUID, taskRunnable);
      return taskRunnable.getTaskUUID();
    } catch (Throwable t) {
      if (taskRunnable != null) {
        // Destroy the task initialization in case of failure.
        taskRunnable.getTask().terminate();
        TaskInfo taskInfo = taskRunnable.getTaskInfo();
        if (taskInfo.getTaskState() != TaskInfo.State.Failure) {
          taskInfo.setTaskState(TaskInfo.State.Failure);
          taskInfo.save();
        }
      }
      String msg = "Error processing " + taskType + " task for " + taskParams.toString();
      log.error(msg, t);
      if (t instanceof PlatformServiceException) {
        throw t;
      }
      throw new RuntimeException(msg, t);
    }
  }

  /**
   * Triggers task abort asynchronously. It can take some time for the task to abort. Caller can
   * check the task status for the final state.
   *
   * @param taskUUID the UUID of the task to be aborted.
   * @return true if the task is found running and abort is triggered successfully, else false.
   */
  public boolean abortTask(UUID taskUUID) {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    if (!isTaskAbortable(taskInfo.getTaskType())) {
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
    Optional<TaskInfo> optional = taskExecutor.abort(taskUUID);
    return optional.isPresent();
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
    RunnableTask runnableTask = runningTasks.get(taskUUID);
    if (runnableTask != null) {
      runnableTask.setTaskExecutionListener(getTaskExecutionListener());
    }
    latch.countDown();
    return true;
  }

  public ObjectNode getStatusOrBadRequest(UUID taskUUID) {
    return mayGetStatus(taskUUID)
        .orElseThrow(
            () -> new PlatformServiceException(BAD_REQUEST, "Not able to find task " + taskUUID));
  }

  public Optional<ObjectNode> buildTaskStatus(
      CustomerTask task, TaskInfo taskInfo, Map<UUID, Set<String>> updatingTasks) {
    if (task == null || taskInfo == null) {
      return Optional.empty();
    }
    ObjectNode responseJson = Json.newObject();
    // Add some generic information about the task
    responseJson.put("title", task.getFriendlyDescription());
    responseJson.put("createTime", task.getCreateTime().toString());
    responseJson.put("target", task.getTargetName());
    responseJson.put("targetUUID", task.getTargetUUID().toString());
    responseJson.put("type", task.getType().name());
    // Find out the state of the task.
    responseJson.put("status", taskInfo.getTaskState().toString());
    // Get the percentage of subtasks that ran and completed
    responseJson.put("percent", taskInfo.getPercentCompleted());
    // Get subtask groups
    UserTaskDetails userTaskDetails = taskInfo.getUserTaskDetails();
    responseJson.set("details", Json.toJson(userTaskDetails));
    // Set abortable if eligible.
    responseJson.put("abortable", false);
    if (taskExecutor.isTaskRunning(task.getTaskUUID())) {
      // Task is abortable only when it is running.
      responseJson.put("abortable", isTaskAbortable(taskInfo.getTaskType()));
    }
    boolean retryable = false;
    // Set retryable if eligible.
    if (isTaskRetryable(taskInfo.getTaskType())
        && task.getTarget().isUniverseTarget()
        && TaskInfo.ERROR_STATES.contains(taskInfo.getTaskState())) {
      Set<String> taskUuidsToAllowRetry =
          updatingTasks.getOrDefault(task.getTargetUUID(), Collections.emptySet());
      retryable = taskUuidsToAllowRetry.contains(taskInfo.getTaskUUID().toString());
    }
    responseJson.put("retryable", retryable);
    if (pauseLatches.containsKey(taskInfo.getTaskUUID())) {
      // Set this only if it is true. The thread is just parking. From the task state
      // perspective, it is still running.
      responseJson.put("paused", true);
    }
    return Optional.of(responseJson);
  }

  public Optional<ObjectNode> mayGetStatus(UUID taskUUID) {
    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    // Check if the task is in the DB.
    TaskInfo taskInfo = TaskInfo.get(taskUUID);
    if (task == null || taskInfo == null) {
      // We are not able to find the task. Report an error.
      log.error("Error fetching task progress for {}. TaskInfo is not found", taskUUID);
      return Optional.empty();
    }
    Map<UUID, Set<String>> updatingTaskByTargetMap = new HashMap<>();
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
    return buildTaskStatus(task, taskInfo, updatingTaskByTargetMap);
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

  public JsonNode getTaskDetails(UUID taskUUID) {
    TaskInfo taskInfo = TaskInfo.get(taskUUID);
    if (taskInfo != null) {
      return taskInfo.getTaskDetails();
    } else {
      // TODO: push this down to TaskInfo
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to retrieve task params for Task UUID: " + taskUUID);
    }
  }

  private int getSubTaskPositionFromContext(String property) {
    int position = -1;
    String value = (String) MDC.get(property);
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
    TaskExecutionListener listener = null;
    Consumer<TaskInfo> beforeTaskConsumer = getBeforeTaskConsumer();
    if (beforeTaskConsumer != null) {
      listener =
          new TaskExecutionListener() {
            @Override
            public void beforeTask(TaskInfo taskInfo) {
              log.info("About to execute task {}", taskInfo);
              beforeTaskConsumer.accept(taskInfo);
            }

            @Override
            public void afterTask(TaskInfo taskInfo, Throwable t) {
              log.info("Task {} is completed", taskInfo);
            }
          };
    }
    return listener;
  }

  // Returns the composed for before task callback of TaskExecutionListener.
  private Consumer<TaskInfo> getBeforeTaskConsumer() {
    Consumer<TaskInfo> consumer = null;
    final int subTaskAbortPosition = getSubTaskPositionFromContext(SUBTASK_ABORT_POSITION_PROPERTY);
    final int subTaskPausePosition = getSubTaskPositionFromContext(SUBTASK_PAUSE_POSITION_PROPERTY);
    if (subTaskAbortPosition >= 0) {
      // Handle abort of subtask.
      Consumer<TaskInfo> abortConsumer =
          taskInfo -> {
            if (taskInfo.getPosition() >= subTaskAbortPosition) {
              log.debug("Aborting task {} at position {}", taskInfo, taskInfo.getPosition());
              throw new CancellationException("Subtask cancelled");
            }
          };
      consumer = abortConsumer;
    }
    if (subTaskPausePosition >= 0) {
      // Handle pause of subtask.
      Consumer<TaskInfo> pauseConsumer =
          taskInfo -> {
            if (taskInfo.getPosition() >= subTaskPausePosition) {
              log.debug("Pausing task {} at position {}", taskInfo, taskInfo.getPosition());
              final UUID subTaskUUID = taskInfo.getParentUUID();
              try {
                // Insert if absent and get the latch.
                pauseLatches.computeIfAbsent(subTaskUUID, k -> new CountDownLatch(1)).await();
              } catch (InterruptedException e) {
                throw new CancellationException("Subtask cancelled: " + e.getMessage());
              } finally {
                pauseLatches.remove(subTaskUUID);
              }
            }
          };
      consumer = consumer == null ? pauseConsumer : consumer.andThen(pauseConsumer);
    }
    return consumer;
  }

  /**
   * A progress monitor to constantly write a last updated timestamp in the DB so that this process
   * and all its subtasks are considered to be alive.
   */
  @Slf4j
  @Singleton
  private static class ProgressMonitor {

    private static final String YB_COMMISSIONER_PROGRESS_CHECK_INTERVAL =
        "yb.commissioner.progress_check_interval";
    private final Scheduler scheduler;
    private final RuntimeConfigFactory runtimeConfigFactory;
    private final ExecutionContext executionContext;

    @Inject
    public ProgressMonitor(
        ActorSystem actorSystem,
        RuntimeConfigFactory runtimeConfigFactory,
        ExecutionContext executionContext) {
      this.scheduler = actorSystem.scheduler();
      this.runtimeConfigFactory = runtimeConfigFactory;
      this.executionContext = executionContext;
    }

    public void start(Map<UUID, RunnableTask> runningTasks) {
      Duration checkInterval = this.progressCheckInterval();
      if (checkInterval.isZero()) {
        log.info(YB_COMMISSIONER_PROGRESS_CHECK_INTERVAL + " set to 0.");
        log.warn("!!! TASK GC DISABLED !!!");
      } else {
        AtomicBoolean isRunning = new AtomicBoolean();
        log.info("Scheduling Progress Check every " + checkInterval);
        scheduler.schedule(
            Duration.ZERO, // InitialDelay
            checkInterval,
            () -> {
              if (isRunning.compareAndSet(false, true)) {
                try {
                  scheduleRunner(runningTasks);
                } finally {
                  isRunning.set(false);
                }
              } else {
                log.warn("Skipping task heartbeating as it is running.");
              }
            },
            this.executionContext);
      }
    }

    private void scheduleRunner(Map<UUID, RunnableTask> runningTasks) {
      // Loop through all the active tasks.
      try {
        Iterator<Entry<UUID, RunnableTask>> iter = runningTasks.entrySet().iterator();
        while (iter.hasNext()) {
          Entry<UUID, RunnableTask> entry = iter.next();
          RunnableTask taskRunnable = entry.getValue();
          // If the task is still running, update its latest timestamp as a part of the heartbeat.
          if (taskRunnable.isTaskRunning()) {
            taskRunnable.doHeartbeat();
          } else if (taskRunnable.hasTaskCompleted()) {
            log.info(
                "Task {} has completed with {} state.", taskRunnable, taskRunnable.getTaskState());
            // Remove task from the set of live tasks.
            iter.remove();
          }
        }
        // TODO: Scan the DB for tasks that have failed to make progress and claim one if possible.
      } catch (Exception e) {
        log.error("Error running commissioner progress checker", e);
      }
    }

    private Duration progressCheckInterval() {
      return runtimeConfigFactory
          .staticApplicationConf()
          .getDuration(YB_COMMISSIONER_PROGRESS_CHECK_INTERVAL);
    }
  }
}
