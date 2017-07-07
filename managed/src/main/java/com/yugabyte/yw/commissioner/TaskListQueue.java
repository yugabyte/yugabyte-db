// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskState;

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
      taskList.setUserSubTaskState(SubTaskState.Running);
      try {
        taskList.run();
        success = taskList.waitFor();
      } catch (Throwable t) {
        // Update task state to failure
        taskList.setUserSubTaskState(SubTaskState.Failure);
        throw t;
      }
      if (!success) {
        LOG.error("TaskList '{}' waitFor() returned failed status.", taskList.toString());
        taskList.setUserSubTaskState(SubTaskState.Failure);
        throw new RuntimeException(taskList.toString() + " failed.");
      }
      taskList.setUserSubTaskState(SubTaskState.Success);
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
        subTask.setState(taskListFailure ? SubTaskState.Unknown:
                                           taskList.getUserSubTaskState());
        userTaskDetails.add(subTask);
      } else if (taskList.getUserSubTaskState() == SubTaskState.Failure ||
                 subTask.getState().equals(SubTaskState.Failure.name())) {
        // If this subtask failed, make sure its state remains in Failure.
        taskListFailure = true;
        subTask.setState(SubTaskState.Failure);
      } else if (taskListFailure) {
        // If an earlier subtask has failed, do not set a state.
        subTask.setState(SubTaskState.Unknown);
        continue;
      } else if (taskList.getUserSubTaskState() == SubTaskState.Running) {
        // Earlier subtasks have not failed. If the current subtask is running, set the bucket to
        // running.
        subTask.setState(SubTaskState.Running);
      }
      userTasksMap.put(subTaskType, subTask);

      // If a subtask has failed, we do not need to set state of any following task. Record the fact
      // that a subtask has failed.
      if (taskList.getUserSubTaskState() == SubTaskState.Failure) {
        taskListFailure = true;
      }
    }
    return userTaskDetails;
  }
}
