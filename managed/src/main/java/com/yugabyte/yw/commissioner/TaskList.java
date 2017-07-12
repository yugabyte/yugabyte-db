// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.LinkedList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;

import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class TaskList implements Runnable {

  public static final Logger LOG = LoggerFactory.getLogger(TaskList.class);

  // User facing subtask. If this field is 'Invalid', the state of this task list  should
  // not be exposed to the user. Note that multiple task lists can be combined into a single user
  // facing entry by providing the same subtask id.
  private UserTaskDetails.SubTaskGroupType subTaskGroupType = UserTaskDetails.SubTaskGroupType.Invalid;

  // The state of the task to be displayed to the user.
  private TaskInfo.State userSubTaskState = TaskInfo.State.Initializing;

  // Task list name.
  private String name;

  // The list of tasks in this task list.
  private List<ITask> taskList;

  // The list of futures to wait for.
  private List<Future<?>> futuresList;

  private AtomicInteger numTasksCompleted;

  private List<TaskInfo> taskInfoList;

  // The number of threads to run in parallel.
  int numThreads;

  // The threadpool executor in case parallel execution is requested.
  ExecutorService executor;

  // Flag to denote the task is done.
  boolean tasksDone = false;

  /**
   * Creates the task list.
   *
   * @param name     : Name for the task list, used to name the threads.
   * @param executor : The threadpool to run the task on.
   */
  public TaskList(String name, ExecutorService executor) {
    this.name = name;
    this.executor = executor;
    this.taskList = new LinkedList<ITask>();
    this.futuresList = new LinkedList<Future<?>>();
    this.numTasksCompleted = new AtomicInteger(0);
    this.taskInfoList = new LinkedList<TaskInfo>();
  }

  public synchronized void setSubTaskGroupType(UserTaskDetails.SubTaskGroupType subTaskGroupType) {
    this.subTaskGroupType = subTaskGroupType;
    for (TaskInfo taskInfo : taskInfoList) {
      taskInfo.setSubTaskGroupType(subTaskGroupType);
      taskInfo.save();
    }
  }

  public UserTaskDetails.SubTaskGroupType getSubTaskGroupType() {
    return subTaskGroupType;
  }

  public synchronized void setUserSubTaskState(TaskInfo.State userTaskState) {
    this.userSubTaskState = userTaskState;
    for (TaskInfo taskInfo : taskInfoList) {
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

  public void addTask(ITask task) {
    LOG.info("Adding task #" + taskList.size() + ": " + task.toString());
    taskList.add(task);
    // Set up corresponding TaskInfo.
    TaskType taskType = TaskType.valueOf(task.getClass().getSimpleName());
    TaskInfo taskInfo = new TaskInfo(taskType);
    taskInfo.setTaskDetails(task.getTaskDetails());
    // Set the owner info in the TaskInfo.
    String hostname = "";
    try {
      hostname = InetAddress.getLocalHost().getHostName();
    } catch (UnknownHostException e) {
      LOG.error("Could not determine the hostname", e);
    }
    taskInfo.setOwner(hostname);
    // Set the SubTaskGroupType in TaskInfo
    if (this.subTaskGroupType != null) {
      taskInfo.setSubTaskGroupType(this.subTaskGroupType);
    }
    taskInfo.save();
    taskInfoList.add(taskInfo);
  }

  public int getNumTasks() {
    return taskList.size();
  }

  public int getNumTasksDone() {
    return numTasksCompleted.get();
  }

  public void setTaskContext(int position, UUID userTaskUUID) {
    for (TaskInfo taskInfo : taskInfoList) {
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
    if (taskList.isEmpty()) {
      LOG.error("No tasks in task list {}.", getName());
      tasksDone = true;
      return;
    }
    LOG.info("Running task list {}.", getName());
    for (ITask task : taskList) {
      Future<?> future = executor.submit(task);
      futuresList.add(future);
    }
  }

  public boolean waitFor() {
    for (Future<?> future : futuresList) {
      // Wait for each future to finish.
      try {
        if (future.get() == null) {
          // Task succeeded.
          numTasksCompleted.incrementAndGet();
        } else {
          LOG.error("ERROR: task {} get() returned null.", future.toString());
          return false;
        }
      } catch (InterruptedException e) {
        LOG.error("Failed to execute task {}, hit error {}.",
                  future.toString(), e.getMessage(), e);
        return false;
      } catch (ExecutionException e) {
        LOG.error("Failed to execute task {}, hit error {}.",
                  future.toString(), e.getMessage(), e);
        return false;
      }
    }

    return true;
  }
}
