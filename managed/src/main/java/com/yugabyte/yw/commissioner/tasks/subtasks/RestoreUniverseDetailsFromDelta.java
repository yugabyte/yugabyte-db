// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Restores edit-owned {@code universe_details_json} topology from the pre-task {@code before} state
 * reconstructed via {@link StateTransitionDetails#applyBeforeTo}. Lock fields, YBC version flags,
 * and {@code sequenceNumber} are taken from the current universe so an intervening YBC upgrade or
 * consistency-check bump is not undone.
 */
@Slf4j
public class RestoreUniverseDetailsFromDelta extends UniverseTaskBase {

  @Inject
  protected RestoreUniverseDetailsFromDelta(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public StateTransitionDetails stateTransitionDetails;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    StateTransitionDetails details = taskParams().stateTransitionDetails;
    if (details == null) {
      throw new IllegalStateException(
          "Cannot restore universe details: state_transition_details is null");
    }
    saveUniverseDetails(
        universe -> {
          UniverseDefinitionTaskParams current = universe.getUniverseDetails();
          UniverseDefinitionTaskParams before = details.applyBeforeTo(current);
          universe.setUniverseDetails(before);
          log.info(
              "Restored universe_details_json from delta for universe {}",
              taskParams().getUniverseUUID());
        });
  }
}
