// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static play.mvc.Http.Status.BAD_REQUEST;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadFactory;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.inject.ApplicationLifecycle;
import play.libs.Json;
import scala.concurrent.ExecutionContext;

@Singleton
public class Commissioner {

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
   * Creates a new task runnable to run the required task, and submits it to a threadpool if needed.
   */
  public UUID submit(TaskType taskType, ITaskParams taskParams) {
    try {
      // Claim the task if we can - check if we will go above the max local concurrent task
      // threshold. If we can claim it, set ourselves as the owner of the task. Otherwise, do not
      // claim the task so that some other process can claim it.
      // TODO: enforce a limit on number of tasks here.
      boolean claimTask = true;

      // Create the task runnable object based on the various parameters passed in.
      RunnableTask taskRunnable = taskExecutor.createRunnableTask(taskType, taskParams);

      if (claimTask) {
        // If we had claimed ownership of the task, submit it to the task threadpool.
        UUID taskUUID = taskExecutor.submit(taskRunnable, executor);
        // Add this task to our queue.
        runningTasks.put(taskUUID, taskRunnable);
      }
      return taskRunnable.getTaskUUID();
    } catch (Throwable t) {
      String msg = "Error processing " + taskType + " task for " + taskParams.toString();
      LOG.error(msg, t);
      throw new RuntimeException(msg, t);
    }
  }

  public boolean abort(UUID taskUUID) {
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
