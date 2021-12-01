// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import java.util.concurrent.ExecutorService;
import play.api.Play;

/**
 * This is deprecated because it is adapted to make existing subtasks work.
 *
 * @deprecated Use createSubTaskGroup(String) of TaskExecutor to get SubTaskGroup.
 */
@Deprecated
public class SubTaskGroup {
  private final TaskExecutor.SubTaskGroup subTaskGroup;

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
   * @param executorService : The threadpool to run the task on.
   * @param ignoreErrors : Flag to tell if an error needs to be thrown if the subTask fails.
   */
  public SubTaskGroup(String name, ExecutorService executorService, boolean ignoreErrors) {
    TaskExecutor taskExecutor = Play.current().injector().instanceOf(TaskExecutor.class);
    this.subTaskGroup =
        taskExecutor.createSubTaskGroup(name, SubTaskGroupType.Invalid, ignoreErrors);
    this.subTaskGroup.setSubTaskExecutor(executorService);
  }

  public TaskExecutor.SubTaskGroup getSubTaskGroup() {
    return subTaskGroup;
  }

  public synchronized void setSubTaskGroupType(SubTaskGroupType subTaskGroupType) {
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
  }

  public String getName() {
    return subTaskGroup.getName();
  }

  @Override
  public String toString() {
    return getName() + " : completed " + getNumTasksDone() + " out of " + getNumTasks() + " tasks.";
  }

  public void addTask(ITask task) {
    subTaskGroup.addSubTask(task);
  }

  public int getNumTasks() {
    return subTaskGroup.getSubTaskCount();
  }

  public int getNumTasksDone() {
    return subTaskGroup.getTasksCompletedCount();
  }
}
