// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
/** Details for {@link com.yugabyte.yw.models.TaskInfo.class} */
public class TaskDetails {
  private TaskError error;

  /** Define all the task error codes here. */
  public static enum TaskErrorCode {
    INTERNAL_ERROR,
    PLATFORM_SHUTDOWN,
    PLATFORM_RESTARTED;
  }

  @Getter
  @Setter
  public static class TaskError {
    private TaskErrorCode code = TaskErrorCode.INTERNAL_ERROR;
    private String message;
  }
}
