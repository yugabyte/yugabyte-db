package com.yugabyte.yw.commissioner;

import java.util.concurrent.ExecutorService;

import com.yugabyte.yw.models.helpers.TaskType;

public interface ExecutorServiceProvider {
  ExecutorService getExecutorServiceFor(TaskType taskType);
}
