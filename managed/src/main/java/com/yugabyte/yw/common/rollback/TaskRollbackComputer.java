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
   * Whether rollback for this source task type is feature-enabled (runtime config / product gate).
   * Used by listing {@code canRollback} and submit eligibility so the UI does not offer Rollback
   * when the matching flag is off. Default true for computers with no separate feature flag.
   */
  default boolean isEnabled() {
    return true;
  }

  /**
   * Build the rollback submission for the failed task described by {@code context}.
   *
   * @throws com.yugabyte.yw.common.PlatformServiceException if rollback cannot proceed
   */
  RollbackSubmission compute(RollbackContext context);
}
