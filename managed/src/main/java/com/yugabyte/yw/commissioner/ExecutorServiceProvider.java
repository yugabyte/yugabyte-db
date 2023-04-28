// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.models.helpers.TaskType;
import java.util.concurrent.ExecutorService;

public interface ExecutorServiceProvider {
  ExecutorService getExecutorServiceFor(TaskType taskType);
}
