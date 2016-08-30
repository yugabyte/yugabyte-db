// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class TaskListQueue {

  public static final Logger LOG = LoggerFactory.getLogger(TaskListQueue.class);

  // The list of tasks lists in this task list sequence.
  CopyOnWriteArrayList<TaskList> taskLists = new CopyOnWriteArrayList<TaskList>();

  /**
   * Add a task list to this sequence.
   */
  public boolean add(TaskList taskList) {
    return taskLists.add(taskList);
  }

  /**
   * Execute the sequence of task lists in a sequential manner.
   */
  public void run() {
    boolean success = false;
    for (TaskList taskList : taskLists) {
      taskList.setUserSubTaskState(UserTaskDetails.SubTaskState.Running);
      try {
        taskList.run();
        success = taskList.waitFor();
      } catch (Throwable t) {
        // Update task state to failure
        taskList.setUserSubTaskState(UserTaskDetails.SubTaskState.Failure);
        throw t;
      }
      if (!success) {
        LOG.error("TaskList '{}' waitFor() returned failed status.", taskList.toString());
        taskList.setUserSubTaskState(UserTaskDetails.SubTaskState.Failure);
        throw new RuntimeException(taskList.toString() + " failed.");
      }
      taskList.setUserSubTaskState(UserTaskDetails.SubTaskState.Success);
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
    Map<UserTaskDetails.SubTaskType, UserTaskDetails.SubTaskDetails> userTasksMap =
        new HashMap<UserTaskDetails.SubTaskType, UserTaskDetails.SubTaskDetails>();
    boolean taskListFailure = false;
    for (TaskList taskList : taskLists) {
      UserTaskDetails.SubTaskType subTaskType = taskList.getUserSubTask();
      if (subTaskType == UserTaskDetails.SubTaskType.Invalid) {
        continue;
      }
      UserTaskDetails.SubTaskDetails subTask = userTasksMap.get(subTaskType);
      if (subTask == null) {
        subTask = UserTaskDetails.createSubTask(subTaskType);
        subTask.setState(taskListFailure ? UserTaskDetails.SubTaskState.Unknown:
                                           taskList.getUserSubTaskState());
        userTaskDetails.add(subTask);
      } else {
        // We may be combining multiple task lists into one user facing bucket. Task lists always
        // finish in sequence.

        // If an earlier subtask has failed, do not set a state.
        if (taskListFailure) {
          subTask.setState(UserTaskDetails.SubTaskState.Unknown);
          continue;
        }
        // Earlier subtasks have not failed. If the current subtask is running, set the bucket to
        // running.
        if (taskList.getUserSubTaskState() == UserTaskDetails.SubTaskState.Running) {
          subTask.setState(UserTaskDetails.SubTaskState.Running);
        }
      }
      userTasksMap.put(subTaskType, subTask);

      // If a subtask has failed, we do not need to set state of any following task. Record the fact
      // that a subtask has failed.
      if (taskList.getUserSubTaskState() == UserTaskDetails.SubTaskState.Failure) {
        taskListFailure = true;
      }
    }
    return userTaskDetails;
  }
}
