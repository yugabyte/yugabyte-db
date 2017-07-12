// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CopyOnWriteArrayList;

import com.yugabyte.yw.models.TaskInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class TaskListQueue {

  public static final Logger LOG = LoggerFactory.getLogger(TaskListQueue.class);

  // The list of tasks lists in this task list sequence.
  CopyOnWriteArrayList<TaskList> taskLists = new CopyOnWriteArrayList<TaskList>();

  private UUID userTaskUUID;

  public TaskListQueue(UUID userTaskUUID) {
    this.userTaskUUID = userTaskUUID;
  }

  /**
   * Add a task list to this sequence.
   */
  public boolean add(TaskList taskList) {
    taskList.setTaskContext(taskLists.size(), userTaskUUID);
    return taskLists.add(taskList);
  }

  /**
   * Execute the sequence of task lists in a sequential manner.
   */
  public void run() {
    boolean success = false;
    for (TaskList taskList : taskLists) {
      taskList.setUserSubTaskState(TaskInfo.State.Running);
      try {
        taskList.run();
        success = taskList.waitFor();
      } catch (Throwable t) {
        // Update task state to failure
        taskList.setUserSubTaskState(TaskInfo.State.Failure);
        throw t;
      }
      if (!success) {
        LOG.error("TaskList '{}' waitFor() returned failed status.", taskList.toString());
        taskList.setUserSubTaskState(TaskInfo.State.Failure);
        throw new RuntimeException(taskList.toString() + " failed.");
      }
      taskList.setUserSubTaskState(TaskInfo.State.Success);
    }
  }

  /**
   * Returns the aggregate percentage completion across all the tasks.
   * @return a number between 0 and 100.
   */
  public int getPercentCompleted() {
    int numTasks = 0;
    int numTasksDone = 0;
    for (TaskList taskList : taskLists) {
      numTasks += taskList.getNumTasks();
      numTasksDone += taskList.getNumTasksDone();
      LOG.debug(taskList.getName() + ": " + taskList.getNumTasksDone() + " / " +
                taskList.getNumTasks() + " tasks completed.");
    }
    if (numTasks == 0) {
      return 0;
    }
    return (int)(numTasksDone * 1.0 / numTasks * 100);
  }

  /**
   * Returns the user facing task details.
   */
  public UserTaskDetails getUserTaskDetails() {
    UserTaskDetails userTaskDetails = new UserTaskDetails();
    Map<UserTaskDetails.SubTaskGroupType, UserTaskDetails.SubTaskDetails> userTasksMap =
        new HashMap<UserTaskDetails.SubTaskGroupType, UserTaskDetails.SubTaskDetails>();
    boolean taskListFailure = false;
    for (TaskList taskList : taskLists) {
      UserTaskDetails.SubTaskGroupType subTaskGroupType = taskList.getSubTaskGroupType();
      if (subTaskGroupType == UserTaskDetails.SubTaskGroupType.Invalid) {
        continue;
      }
      UserTaskDetails.SubTaskDetails subTask = userTasksMap.get(subTaskGroupType);
      if (subTask == null) {
        subTask = UserTaskDetails.createSubTask(subTaskGroupType);
        subTask.setState(taskListFailure ? TaskInfo.State.Unknown:
                                           taskList.getUserSubTaskState());
        userTaskDetails.add(subTask);
      } else if (taskList.getUserSubTaskState() == TaskInfo.State.Failure ||
                 subTask.getState().equals(TaskInfo.State.Failure.name())) {
        // If this subtask failed, make sure its state remains in Failure.
        taskListFailure = true;
        subTask.setState(TaskInfo.State.Failure);
      } else if (taskListFailure) {
        // If an earlier subtask has failed, do not set a state.
        subTask.setState(TaskInfo.State.Unknown);
        continue;
      } else if (taskList.getUserSubTaskState() == TaskInfo.State.Running) {
        // Earlier subtasks have not failed. If the current subtask is running, set the bucket to
        // running.
        subTask.setState(TaskInfo.State.Running);
      }
      userTasksMap.put(subTaskGroupType, subTask);

      // If a subtask has failed, we do not need to set state of any following task. Record the fact
      // that a subtask has failed.
      if (taskList.getUserSubTaskState() == TaskInfo.State.Failure) {
        taskListFailure = true;
      }
    }
    return userTaskDetails;
  }
}
