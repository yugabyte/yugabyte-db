// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.Vector;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class TaskListQueue {

  public static final Logger LOG = LoggerFactory.getLogger(TaskListQueue.class);

  // The list of tasks lists in this task list sequence.
  Vector<TaskList> taskLists = new Vector<TaskList>();

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
    boolean status = true;
    for (TaskList taskList : taskLists) {
      taskList.run();
      status = taskList.waitFor();

      if (!status) {
        LOG.error("TaskList '{}' waitFor() returned failed status.", taskList.toString());

        throw new RuntimeException(taskList.toString() + " failed.");
      }
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
}
