// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.helpers.YBAError.Code;
import lombok.Getter;

/** Base class for task execution exception. */
public class TaskExecutionException extends RuntimeException {
  @Getter private Code code;

  public TaskExecutionException(Code code, String message) {
    super(message);
    this.code = code;
  }
}
