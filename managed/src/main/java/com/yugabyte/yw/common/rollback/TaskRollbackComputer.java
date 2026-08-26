// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

/**
 * Computes how to roll back a failed task of a specific source {@link
 * com.yugabyte.yw.models.helpers.TaskType}.
 *
 * <p>One implementation is bound per source TaskType via Guice {@code MapBinder} in {@link
 * TaskRollbackModule}.
 */
public interface TaskRollbackComputer {

  /**
   * Build the rollback submission for the failed task described by {@code context}.
   *
   * @throws com.yugabyte.yw.common.PlatformServiceException if rollback cannot proceed
   */
  RollbackSubmission compute(RollbackContext context);
}
