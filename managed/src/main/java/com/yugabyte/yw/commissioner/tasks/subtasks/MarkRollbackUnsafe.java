// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Flips {@code state_transition_details.rollbackSafe} to false when the task crosses the rollback
 * checkpoint (first mutation of an existing running server). Idempotent if details are null or
 * already unsafe.
 */
@Slf4j
public class MarkRollbackUnsafe extends UniverseTaskBase {

  @Inject
  protected MarkRollbackUnsafe(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    saveUniverseDetails(
        (Universe.UniverseUpdater)
            universe -> {
              StateTransitionDetails details = universe.getStateTransitionDetails();
              if (details == null || !details.isRollbackSafe()) {
                log.debug(
                    "MarkRollbackUnsafe no-op for universe {}: details={}",
                    taskParams().getUniverseUUID(),
                    details);
                return;
              }
              details.setRollbackSafe(false);
              universe.setStateTransitionDetails(details);
              log.info(
                  "Marked state_transition_details.rollbackSafe=false for universe {}",
                  taskParams().getUniverseUUID());
            });
  }
}
