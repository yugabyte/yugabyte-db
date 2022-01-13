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
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadFactory;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import play.inject.ApplicationLifecycle;
import play.libs.Json;
import scala.concurrent.ExecutionContext;

@Singleton
public class Commissioner {

  public static final String SUBTASK_ABORT_POSITION_PROPERTY = "subtask-abort-position";

  public static final Logger LOG = LoggerFactory.getLogger(Commissioner.class);

  private final ExecutorService executor;

  private final TaskExecutor taskExecutor;

  // A map of all task UUID's to the task runnable objects for all the user tasks that are currently
  // active. Recently completed tasks are also in this list, their completion percentage should be
  // persisted before removing the task from this map.
  private final Map<UUID, RunnableTask> runningTasks = new ConcurrentHashMap<>();

  @Inject
  public Commissioner(
      ProgressMonitor progressMonitor,
      ApplicationLifecycle lifecycle,
      PlatformExecutorFactory platformExecutorFactory,
      TaskExecutor taskExecutor) {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-%d").build();
    this.taskExecutor = taskExecutor;
    executor = platformExecutorFactory.createExecutor("commissioner", namedThreadFactory);
    LOG.info("Started Commissioner TaskPool.");
    progressMonitor.start(runningTasks);
    LOG.info("Started TaskProgressMonitor thread.");
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
    try {
      final int subTaskAbortPosition = getSubTaskAbortPosition();
      // Create the task runnable object based on the various parameters passed in.
      RunnableTask taskRunnable = taskExecutor.createRunnableTask(taskType, taskParams);
      if (subTaskAbortPosition >= 0) {
        taskRunnable.setTaskExecutionListener(
            new TaskExecutionListener() {
              @Override
              public void beforeTask(TaskInfo taskInfo) {
                if (taskInfo.getPosition() >= subTaskAbortPosition) {
                  LOG.debug("Aborting task {} at position {}", taskInfo, taskInfo.getPosition());
                  throw new CancellationException("Subtask cancelled");
                }
              }

              @Override
              public void afterTask(TaskInfo taskInfo, Throwable t) {
                LOG.info("Task {} is completed", taskInfo);
              }
            });
      }
      UUID taskUUID = taskExecutor.submit(taskRunnable, executor);
      // Add this task to our queue.
      runningTasks.put(taskUUID, taskRunnable);
      return taskRunnable.getTaskUUID();
    } catch (Throwable t) {
      String msg = "Error processing " + taskType + " task for " + taskParams.toString();
      LOG.error(msg, t);
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
      LOG.warn("Task {} is not running", taskUUID);
      return false;
    }
    Optional<TaskInfo> optional = taskExecutor.abort(taskUUID);
    return optional.isPresent();
  }

  public ObjectNode getStatusOrBadRequest(UUID taskUUID) {
    return mayGetStatus(taskUUID)
        .orElseThrow(
            () -> new PlatformServiceException(BAD_REQUEST, "Not able to find task " + taskUUID));
  }

  public Optional<ObjectNode> mayGetStatus(UUID taskUUID) {
    ObjectNode responseJson = Json.newObject();

    // Check if the task is in the DB
    TaskInfo taskInfo = TaskInfo.get(taskUUID);
    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    if (taskInfo != null && task != null) {
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
      if (taskExecutor.isTaskRunning(taskUUID)) {
        // Task is abortable only when it is running.
        responseJson.put("abortable", isTaskAbortable(taskInfo.getTaskType()));
      }
      // Set retryable if eligible.
      responseJson.put("retryable", false);
      if (isTaskRetryable(taskInfo.getTaskType())
          && (task.getTarget() == TargetType.Universe || task.getTarget() == TargetType.Cluster)
          && TaskInfo.ERROR_STATES.contains(taskInfo.getTaskState())) {
        // Retryable depends on the updating task UUID in the Universe.
        Universe.maybeGet(task.getTargetUUID())
            .ifPresent(
                u ->
                    responseJson.put(
                        "retryable", taskUUID.equals(u.getUniverseDetails().updatingTaskUUID)));
      }
      return Optional.of(responseJson);
    }

    // We are not able to find the task. Report an error.
    LOG.error(
        "Error fetching Task Progress for " + taskUUID + ", TaskInfo with that taskUUID not found");
    return Optional.empty();
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

  private int getSubTaskAbortPosition() {
    int abortPosition = -1;
    String value = (String) MDC.get(SUBTASK_ABORT_POSITION_PROPERTY);
    if (!Strings.isNullOrEmpty(value)) {
      try {
        abortPosition = Integer.parseInt(value);
      } catch (NumberFormatException e) {
        LOG.warn("Error in parsing subtask abort position, ignoring it.", e);
        abortPosition = -1;
      }
    }
    return abortPosition;
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
        log.info("Scheduling Progress Check every " + checkInterval);
        scheduler.schedule(
            Duration.ZERO, // InitialDelay
            checkInterval,
            () -> scheduleRunner(runningTasks),
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
          } else if (taskRunnable.hasTaskSucceeded()) {
            LOG.info("Task " + taskRunnable.toString() + " has succeeded.");
            // Remove task from the set of live tasks.
            iter.remove();
          } else if (taskRunnable.hasTaskFailed()) {
            LOG.info("Task " + taskRunnable.toString() + " has failed.");
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
