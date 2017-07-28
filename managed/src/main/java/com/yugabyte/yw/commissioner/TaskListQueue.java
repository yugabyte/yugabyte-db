// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

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
}
