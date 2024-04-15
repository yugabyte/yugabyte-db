// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.helpers.TaskDetails.TaskErrorCode;
import lombok.Getter;

/** Base class for task execution exception. */
public class TaskExecutionException extends RuntimeException {
  @Getter private TaskErrorCode code;

  public TaskExecutionException(TaskErrorCode code, String message) {
    super(message);
    this.code = code;
  }
}
