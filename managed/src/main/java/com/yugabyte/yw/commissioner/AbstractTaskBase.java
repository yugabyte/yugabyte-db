// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.forms.ITaskParams;

import play.libs.Json;

public abstract class AbstractTaskBase implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(AbstractTaskBase.class);

  // The params for this task.
  protected ITaskParams taskParams;

  // The threadpool on which the tasks are executed.
  protected ExecutorService executor;

  // The sequence of task lists that should be executed.
  protected TaskListQueue taskListQueue;

  protected ITaskParams taskParams() {
    return taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = params;
  }

  @Override
  public String getName() {
    return this.getClass().getSimpleName();
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public String toString() {
    return getName() + " : details=" + getTaskDetails();
  }

  @Override
  public abstract void run();

  @Override
  public int getPercentCompleted() {
    if (taskListQueue == null) {
      return 0;
    }
    return taskListQueue.getPercentCompleted();
  }

  @Override
  public UserTaskDetails getUserTaskDetails() {
    if (taskListQueue == null) {
      return null;
    }
    return taskListQueue.getUserTaskDetails();
  }

  public void createThreadpool(int numThreads) {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
    executor = Executors.newFixedThreadPool(numThreads, namedThreadFactory);
  }
}
