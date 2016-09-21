// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.models.TaskInfo;

/**
 * This class is responsible for creating and running a task. It provides all the common
 * infrastructure across the different types of tasks. It creates and keeps an instance of the
 * ITask object that actually performs the work specific to the current task type.
 */
public class TaskRunner implements Runnable {

  public static final Logger LOG = LoggerFactory.getLogger(TaskRunner.class);

  // This is a map from the task types to the classes so that we can instantiate the task.
  private static Map<TaskInfo.Type, Class<? extends ITask>> taskTypeToTaskClassMap;

  // The data underlying the task.
  private TaskInfo taskInfo;

  // The task object that will run the current task.
  private ITask task;

  static {
    // Initialize the map which holds the task types to their task class.
    Map<TaskInfo.Type, Class<? extends ITask>> typeMap =
        new HashMap<TaskInfo.Type, Class<? extends ITask>>();

    for (TaskInfo.Type taskType : TaskInfo.Type.values()) {
      String className = "com.yugabyte.yw.commissioner.tasks." + taskType.toString();
      Class<? extends ITask> taskClass;
      try {
        taskClass = Class.forName(className).asSubclass(ITask.class);
        typeMap.put(taskType, taskClass);
        LOG.info("Found task: " + className);
      } catch (ClassNotFoundException e) {
        LOG.error("Could not find task for task type " + taskType, e);
      }
    }
    taskTypeToTaskClassMap = Collections.unmodifiableMap(typeMap);
    LOG.info("Done loading tasks.");
  }

  /**
   * Creates the task runner along with the task object and persists the task info info.
   *
   * @param taskType        : the task type
   * @param claimTask       : if true, adds this process as the owner of the task being created
   * @return the TaskRunner object on which run can be called.
   * @throws InstantiationException
   * @throws IllegalAccessException
   */
  public static TaskRunner createTask(TaskInfo.Type taskType,
                                      ITaskParams taskParams,
                                      boolean claimTask)
      throws InstantiationException, IllegalAccessException {

    // Create an instance of the task.
    ITask task = taskTypeToTaskClassMap.get(taskType).newInstance();

    // Init the task.
    task.initialize(taskParams);

    // Create the task runner object.
    TaskRunner taskRunner = new TaskRunner(taskType, task);

    // Persist the task in the queue.
    taskRunner.save();
    LOG.info("Created task, details: " + taskRunner.toString());

    return taskRunner;
  }

  private TaskRunner(TaskInfo.Type taskType, ITask task) {
    this.task = task;
    // Create a new task info object.
    taskInfo = new TaskInfo(taskType);
    // Set the task details.
    taskInfo.setTaskDetails(task.getTaskDetails());
    // Set the owner info.
    String hostname = "";
    try {
      hostname = InetAddress.getLocalHost().getHostName();
    } catch (UnknownHostException e) {
      LOG.error("Could not determine the hostname", e);
    }
    taskInfo.setOwner(hostname);
  }

  public UUID getTaskUUID() {
    return taskInfo.getTaskUUID();
  }

  /**
   * Serializes and saves the task object created so far in the persistent queue.
   */
  public void save() {
    taskInfo.save();
  }

  public boolean isTaskRunning() {
    return taskInfo.getTaskState() == TaskInfo.State.Running;
  }

  public boolean hasTaskSucceeded() {
    return taskInfo.getTaskState() == TaskInfo.State.Success;
  }

  public boolean hasTaskFailed() {
    return taskInfo.getTaskState() == TaskInfo.State.Failure;
  }

  /**
   * This method does two things. First, it updates the timestamp on the task to indicate progress.
   * Second, it gives the underlying task to checkpoint its work if needed.
   */
  public void doHeartbeat() {
    // Set the last updated timestamp of the task to now by force saving it.
    taskInfo.markAsDirty();
    taskInfo.save();
  }

  @Override
  public void run() {
    LOG.info("Running task");
    updateTaskState(TaskInfo.State.Running);
    try {
      // Run the task.
      task.run();

      // Update the task state to success and checkpoint it.
      updateTaskState(TaskInfo.State.Success);
    } catch (Throwable t) {
      LOG.error("Error running task", t);

      // Update the task state to failure and checkpoint it.
      updateTaskState(TaskInfo.State.Failure);
    }
  }

  public String getState() {
    return taskInfo.getTaskState().toString();
  }

  public int getPercentCompleted() {
    // If the task was just created it is zero percent done.
    if (taskInfo.getTaskState() == TaskInfo.State.Created) {
      return 0;
    } else if (taskInfo.getTaskState() == TaskInfo.State.Failure) {
      return 0;
    }  else if (taskInfo.getTaskState() == TaskInfo.State.Success) {
      return 100;
    }  else {
      return task.getPercentCompleted();
    }
  }

  public UserTaskDetails getUserTaskDetails() {
    // If the task was just created it is zero percent done.
    if (taskInfo.getTaskState() == TaskInfo.State.Created) {
      return null;
    } else {
      return task.getUserTaskDetails();
    }
  }

  /**
   * Updates the task state and saves it to the persistent queue.
   * @param newState
   */
  private void updateTaskState(TaskInfo.State newState) {
    LOG.info("Updating task [" + taskInfo.toString() + "] to new state " + newState);
    taskInfo.setTaskState(newState);
    taskInfo.save();
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("task-info {" + taskInfo.toString() + "}");
    sb.append(", ");
    sb.append("task {" + task.toString() + "}");
    return sb.toString();
  }
}
