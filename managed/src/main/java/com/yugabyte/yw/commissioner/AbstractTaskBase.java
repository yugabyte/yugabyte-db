// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.UUID;
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

  // The UUID of the top-level user-facing task at the top of Task tree. Eg. CreateUniverse, etc.
  protected UUID userTaskUUID;

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

  public void createThreadpool(int numThreads) {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
    executor = Executors.newFixedThreadPool(numThreads, namedThreadFactory);
  }

  @Override
  public void setUserTaskUUID(UUID userTaskUUID) {
    this.userTaskUUID = userTaskUUID;
  }
}
