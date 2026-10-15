// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import com.yugabyte.yw.models.helpers.TaskType;

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
   * {@link TaskType} submitted by {@link #compute} when it is 1:1 with the source type. Null when
   * the rollback type is chosen at compute time (software upgrade VM vs Kubernetes).
   */
  default TaskType rollbackTaskType() {
    return null;
  }

  /**
   * Whether this rollback needs a captured {@code state_transition_details} checkpoint (edit /
   * add-node style, where rollback replays a before/target delta). When true, a failed task that
   * never reached the freeze/checkpoint - e.g. aborted at precheck - has no checkpoint and is not
   * rollbackable. Non-checkpoint rollbacks (software upgrade) leave this false.
   */
  default boolean requiresStateTransitionDetails() {
    return false;
  }

  /**
   * Build the rollback submission for the failed task described by {@code context}.
   *
   * @throws com.yugabyte.yw.common.PlatformServiceException if rollback cannot proceed
   */
  RollbackSubmission compute(RollbackContext context);
}
