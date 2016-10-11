// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
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

  /**
   * Execute a system command, and output its STDERR to the process log.
   * @param command : the command to execute.
   */
  public void execCommand(String command) {
    LOG.info("Command to run: [" + command + "]");
    try {
      Process p = Runtime.getRuntime().exec(command);

      // Log the stderr output of the process.
      BufferedReader berr = new BufferedReader(new InputStreamReader(p.getErrorStream()));
      String line = null;
      while ((line = berr.readLine()) != null) {
        LOG.warn("[" + getName() + "] STDERR: " + line);
      }
      BufferedReader bout = new BufferedReader(new InputStreamReader(p.getInputStream()));
      while ((line = bout.readLine()) != null) {
        LOG.info("[" + getName() + "] STDOUT: " + line);
      }
      int exitValue = p.waitFor();
      String message = "Command [" + command + "] finished with exit code " + exitValue;
      LOG.info(message);
      if (exitValue != 0) {
        throw new RuntimeException(message);
      }
    } catch (IOException e) {
      LOG.error("Command [" + command + "] threw IOException: {}", e);
      throw new RuntimeException(e);
    } catch (InterruptedException e) {
      LOG.error("Command [" + command + "] threw InterruptedException: {}", e);
      throw new RuntimeException(e);
    }
  }
}
