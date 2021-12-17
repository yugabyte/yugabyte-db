// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import java.util.UUID;
import play.api.Play;

/**
 * This is deprecated because it is adapted to make existing subtasks work.
 *
 * @deprecated Get RunnableTask of to add subtask groups.
 */
@Deprecated
public class SubTaskGroupQueue {

  private final TaskExecutor taskExecutor;
  private final RunnableTask runnableTask;

  public SubTaskGroupQueue(UUID userTaskUUID) {
    taskExecutor = Play.current().injector().instanceOf(TaskExecutor.class);
    runnableTask = taskExecutor.getRunnableTask(userTaskUUID);
  }

  public SubTaskGroupQueue(RunnableTask runnableTask) {
    taskExecutor = Play.current().injector().instanceOf(TaskExecutor.class);
    this.runnableTask = runnableTask;
  }

  public TaskExecutor getTaskExecutor() {
    return taskExecutor;
  }

  /** Add a task list to this sequence. */
  public boolean add(SubTaskGroup subTaskGroup) {
    runnableTask.addSubTaskGroup(subTaskGroup.getSubTaskGroup());
    return true;
  }

  /** Execute the sequence of task lists in a sequential manner. */
  public void run() {
    runnableTask.runSubTasks();
  }
}
