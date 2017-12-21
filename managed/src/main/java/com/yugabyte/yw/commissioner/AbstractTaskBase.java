// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

import com.yugabyte.yw.common.ShellProcessHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.forms.ITaskParams;

import play.libs.Json;

public abstract class AbstractTaskBase implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(AbstractTaskBase.class);

  // Minimum number of concurrent tasks to execute at a time.
  private static final int MIN_TASK_THREADS = 1;

  // Maximum number of concurrent tasks to execute at a time.
  // Not a bigger number to reduce thread spawn causing overload or OOM's.
  private static final int MAX_TASK_THREADS = 25;

  // The maximum time that excess idle threads will wait for new tasks before terminating.
  // The unit is specified in the API (and is seconds).
  private static final long THREAD_ALIVE_TIME = 60L;

  // The params for this task.
  protected ITaskParams taskParams;

  // The threadpool on which the tasks are executed.
  protected ExecutorService executor;

  // The sequence of task lists that should be executed.
  protected SubTaskGroupQueue subTaskGroupQueue;

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

  private static ExecutorService newCachedThreadPool(ThreadFactory namedThreadFactory) {
    return new ThreadPoolExecutor(MIN_TASK_THREADS, MAX_TASK_THREADS, THREAD_ALIVE_TIME,
                                  TimeUnit.SECONDS, new SynchronousQueue<Runnable>(),
                                  namedThreadFactory);
  }

  public void createThreadpool() {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
    executor = newCachedThreadPool(namedThreadFactory);
  }

  @Override
  public void setUserTaskUUID(UUID userTaskUUID) {
    this.userTaskUUID = userTaskUUID;
  }

  /**
   * Log the output of shellResponse to STDOUT or STDERR
   * @param response : ShellResponse object
   */
  public void logShellResponse(ShellProcessHandler.ShellResponse response) {
    if (response.code == 0) {
      LOG.info("[" + getName() + "] STDOUT: " + response.message);
    } else {
      throw new RuntimeException(response.message);
    }
  }
}
