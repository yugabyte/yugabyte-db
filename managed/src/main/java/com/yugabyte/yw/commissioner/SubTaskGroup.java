// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.commissioner.TaskRunner.COMMISSIONER_TASK_EXECUTION_SEC;
import static com.yugabyte.yw.commissioner.TaskRunner.COMMISSIONER_TASK_WAITING_SEC;
import static com.yugabyte.yw.models.helpers.CommonUtils.getDurationSeconds;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.password.RedactingService;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.helpers.TaskType;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;
import lombok.Data;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class SubTaskGroup implements Runnable {

  // User facing subtask. If this field is 'Invalid', the state of this task list  should
  // not be exposed to the user. Note that multiple task lists can be combined into a single user
  // facing entry by providing the same subtask id.
  private UserTaskDetails.SubTaskGroupType subTaskGroupType =
      UserTaskDetails.SubTaskGroupType.Invalid;

  // The state of the task to be displayed to the user.
  private TaskInfo.State userSubTaskState = TaskInfo.State.Initializing;

  // Task list name.
  private final String name;

  // The list of tasks in this task list.
  private final Map<AbstractTaskBase, TaskInfoWithStats> taskMap;

  // The list of futures to wait for.
  private final Map<Future<?>, TaskInfoWithStats> futuresMap;

  private final AtomicInteger numTasksCompleted;

  // The threadpool executor in case parallel execution is requested.
  ExecutorService executor;

  // Flag to denote the task is done.
  boolean tasksDone = false;

  // Flag to denote if an exception needs to be thrown on failure.
  boolean ignoreErrors = false;

  /**
   * Creates the task list.
   *
   * @param name : Name for the task list, used to name the threads.
   * @param executor : The threadpool to run the task on.
   */
  public SubTaskGroup(String name, ExecutorService executor) {
    this(name, executor, false);
  }

  /**
   * Creates the task list.
   *
   * @param name : Name for the task list, used to name the threads.
   * @param executor : The threadpool to run the task on.
   * @param ignoreErrors : Flag to tell if an error needs to be thrown if the subTask fails.
   */
  public SubTaskGroup(String name, ExecutorService executor, boolean ignoreErrors) {
    this.name = name;
    this.executor = executor;
    this.taskMap = new HashMap<>();
    this.futuresMap = new HashMap<>();
    this.numTasksCompleted = new AtomicInteger(0);
    this.ignoreErrors = ignoreErrors;
  }

  public synchronized void setSubTaskGroupType(UserTaskDetails.SubTaskGroupType subTaskGroupType) {
    this.subTaskGroupType = subTaskGroupType;
    for (TaskInfoWithStats taskInfoWithStats : taskMap.values()) {
      TaskInfo taskInfo = taskInfoWithStats.getTaskInfo();
      taskInfo.setSubTaskGroupType(subTaskGroupType);
      taskInfo.save();
    }
  }

  public UserTaskDetails.SubTaskGroupType getSubTaskGroupType() {
    return subTaskGroupType;
  }

  public synchronized void setUserSubTaskState(TaskInfo.State userTaskState) {
    this.userSubTaskState = userTaskState;
    for (TaskInfoWithStats taskInfoWithStats : taskMap.values()) {
      TaskInfo taskInfo = taskInfoWithStats.getTaskInfo();
      taskInfo.setTaskState(userTaskState);
      taskInfo.save();
    }
  }

  public synchronized TaskInfo.State getUserSubTaskState() {
    return userSubTaskState;
  }

  public String getName() {
    return name;
  }

  @Override
  public String toString() {
    return getName() + " : completed " + getNumTasksDone() + " out of " + getNumTasks() + " tasks.";
  }

  public void addTask(AbstractTaskBase task) {
    log.info("Adding task #" + taskMap.size() + ": " + task.getName());
    JsonNode redactedTask = RedactingService.filterSecretFields(task.getTaskDetails());
    log.debug(
        "Details for task #" + taskMap.size() + ": " + task.getName() + "details=" + redactedTask);

    // Set up corresponding TaskInfo.
    TaskType taskType = TaskType.valueOf(task.getClass().getSimpleName());
    TaskInfo taskInfo = new TaskInfo(taskType);
    taskInfo.setTaskDetails(RedactingService.filterSecretFields(task.getTaskDetails()));
    // Set the owner info in the TaskInfo.
    String hostname = "";
    try {
      hostname = InetAddress.getLocalHost().getHostName();
    } catch (UnknownHostException e) {
      log.error("Could not determine the hostname", e);
    }
    taskInfo.setOwner(hostname);
    // Set the SubTaskGroupType in TaskInfo
    if (this.subTaskGroupType != null) {
      taskInfo.setSubTaskGroupType(this.subTaskGroupType);
    }
    taskInfo.save();
    taskMap.put(task, new TaskInfoWithStats(taskInfo));
  }

  public int getNumTasks() {
    return taskMap.size();
  }

  public int getNumTasksDone() {
    return numTasksCompleted.get();
  }

  public void setTaskContext(int position, UUID userTaskUUID) {
    for (TaskInfoWithStats taskInfoWithStats : taskMap.values()) {
      TaskInfo taskInfo = taskInfoWithStats.getTaskInfo();
      taskInfo.setPosition(position);
      taskInfo.setParentUuid(userTaskUUID);
      taskInfo.save();
    }
  }

  /**
   * Asynchronously starts the tasks and returns. To wait for the tasks to complete, call the
   * waitFor() method.
   */
  @Override
  public void run() {
    if (taskMap.isEmpty()) {
      log.error("No tasks in task list {}.", getName());
      tasksDone = true;
      return;
    }
    log.info("Running task list {}.", getName());
    for (Map.Entry<AbstractTaskBase, TaskInfoWithStats> entry : taskMap.entrySet()) {
      TaskInfoWithStats taskInfoWithStats = entry.getValue();
      taskInfoWithStats.setScheduledTime(new Date());
      Future<?> future =
          executor.submit(
              () -> {
                try {
                  taskInfoWithStats.setExecutionStartedTime(new Date());
                  writeTaskWaitMetric(taskInfoWithStats);
                  entry.getKey().run();
                } finally {
                  taskInfoWithStats.setExecutionFinishedTime(new Date());
                }
              });
      // TODO: looks like race condition. Investigate further

      futuresMap.put(future, taskInfoWithStats);
    }
  }

  public boolean waitFor() {
    boolean hasErrored = false;
    // TODO: looks like race condition. Investigate further
    for (Future<?> future : futuresMap.keySet()) {
      TaskInfoWithStats taskInfoWithStats = futuresMap.get(future);
      TaskInfo taskInfo = taskInfoWithStats.getTaskInfo();

      // Wait for each future to finish.
      String errorString = null;
      try {
        if (taskInfo.getTaskType() == TaskType.RunExternalScript) {
          try {
            JsonNode jsonNode = (JsonNode) taskInfo.getTaskDetails();
            long timeLimitMins = Long.parseLong(jsonNode.get("timeLimitMins").asText());
            future.get(timeLimitMins, TimeUnit.MINUTES);
          } catch (TimeoutException e) {
            throw new Exception("External Script execution failed as it exceeds timeLimit");
          }
        }
        if (future.get() == null) {
          // Task succeeded.
          numTasksCompleted.incrementAndGet();
        } else {
          errorString =
              "ERROR: while running task "
                  + taskInfo.toString()
                  + " "
                  + taskInfo.getTaskUUID().toString();
          log.error(errorString);
        }
      } catch (Exception e) {
        taskInfo.setTaskDetails(RedactingService.filterSecretFields(taskInfo.getTaskDetails()));
        errorString =
            "Failed to execute task "
                + StringUtils.abbreviate(taskInfo.getTaskDetails().toString(), 500)
                + ", hit error:\n\n"
                + StringUtils.abbreviateMiddle(e.getMessage(), "...", 3000)
                + ".";
        log.error(
            "Failed to execute task type {} UUID {} details {}, hit error.",
            taskInfo.getTaskType().toString(),
            taskInfo.getTaskUUID().toString(),
            taskInfo.getTaskDetails(),
            e);
      } finally {
        if (errorString != null) {
          hasErrored = true;
          // TODO: Avoid this deepCopy
          ObjectNode details = taskInfo.getTaskDetails().deepCopy();
          details.put("errorString", errorString);
          taskInfo.setTaskDetails(details);
          taskInfo.save();
        }
        if (taskInfoWithStats.getExecutionStartedTime() == null) {
          writeTaskWaitMetric(taskInfoWithStats);
        }
        if (hasErrored) {
          writeTaskFailedMetric(taskInfoWithStats);
        } else {
          writeTaskSuccessMetric(taskInfoWithStats);
        }
      }
    }
    return !hasErrored;
  }

  private void writeTaskWaitMetric(TaskInfoWithStats taskInfoWithStats) {
    Date executionStartedTime =
        taskInfoWithStats.getExecutionStartedTime() != null
            ? taskInfoWithStats.getExecutionStartedTime()
            : new Date();
    COMMISSIONER_TASK_WAITING_SEC
        .labels(taskInfoWithStats.getName())
        .observe(getDurationSeconds(taskInfoWithStats.getScheduledTime(), executionStartedTime));
  }

  private void writeTaskSuccessMetric(TaskInfoWithStats taskInfoWithStats) {
    if (taskInfoWithStats.getExecutionStartedTime() != null
        && taskInfoWithStats.getExecutionFinishedTime() != null) {
      COMMISSIONER_TASK_EXECUTION_SEC
          .labels(taskInfoWithStats.getName(), State.Success.name())
          .observe(
              getDurationSeconds(
                  taskInfoWithStats.getExecutionStartedTime(),
                  taskInfoWithStats.getExecutionFinishedTime()));
    } else {
      log.warn("Started or finished time does not exist for task {}", taskInfoWithStats);
    }
  }

  private void writeTaskFailedMetric(TaskInfoWithStats taskInfoWithStats) {
    if (taskInfoWithStats.getExecutionStartedTime() != null) {
      Date executionFinishedTime =
          taskInfoWithStats.getExecutionFinishedTime() != null
              ? taskInfoWithStats.getExecutionFinishedTime()
              : new Date();
      COMMISSIONER_TASK_EXECUTION_SEC
          .labels(taskInfoWithStats.getName(), State.Failure.name())
          .observe(
              getDurationSeconds(
                  taskInfoWithStats.getExecutionStartedTime(), executionFinishedTime));
    } else {
      log.trace("Started time does not exist for task {}", taskInfoWithStats);
    }
  }

  public void cleanup() {
    for (AbstractTaskBase task : taskMap.keySet()) {
      // Subtasks are also tasks which may have their own executor service
      task.terminate();
    }
  }

  @Data
  @ToString
  private static class TaskInfoWithStats {
    private final TaskInfo taskInfo;
    private Date scheduledTime;
    private Date executionStartedTime;
    private Date executionFinishedTime;

    TaskInfoWithStats(TaskInfo taskInfo) {
      this.taskInfo = taskInfo;
    }

    String getName() {
      return taskInfo.getTaskType().name();
    }

    public synchronized void setScheduledTime(Date scheduledTime) {
      this.scheduledTime = scheduledTime;
    }

    public synchronized void setExecutionStartedTime(Date executionStartedTime) {
      this.executionStartedTime = executionStartedTime;
    }

    public synchronized void setExecutionFinishedTime(Date executionFinishedTime) {
      this.executionFinishedTime = executionFinishedTime;
    }
  }
}
