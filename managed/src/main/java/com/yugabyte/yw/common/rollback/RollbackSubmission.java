// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Objects;
import lombok.Getter;

/**
 * Describes how to submit a rollback for a failed task.
 *
 * <p>{@link #setPreviousTaskUUID} should be true when the rollback is a continuation/retry-linked
 * task (e.g. DR switchover). It must be false for fresh tasks such as software-upgrade downgrade,
 * which must not inherit the failed upgrade's runtimeInfo or be treated as a retry of it.
 */
@Getter
public class RollbackSubmission {
  private final TaskType rollbackTaskType;
  private final AbstractTaskParams params;
  private final CustomerTask.TaskType customerTaskType;
  private final boolean setPreviousTaskUUID;

  public RollbackSubmission(
      TaskType rollbackTaskType,
      AbstractTaskParams params,
      CustomerTask.TaskType customerTaskType) {
    this(rollbackTaskType, params, customerTaskType, true);
  }

  public RollbackSubmission(
      TaskType rollbackTaskType,
      AbstractTaskParams params,
      CustomerTask.TaskType customerTaskType,
      boolean setPreviousTaskUUID) {
    this.rollbackTaskType = Objects.requireNonNull(rollbackTaskType, "rollbackTaskType");
    this.params = Objects.requireNonNull(params, "params");
    this.customerTaskType = Objects.requireNonNull(customerTaskType, "customerTaskType");
    this.setPreviousTaskUUID = setPreviousTaskUUID;
  }
}
