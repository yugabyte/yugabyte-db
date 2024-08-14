// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.function.Consumer;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/** Generic field modifier subtask for universe. */
@Slf4j
public class UpdateUniverseFields extends UniverseTaskBase {

  @Inject
  protected UpdateUniverseFields(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    @JsonIgnore public Consumer<Universe> fieldModifier;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Running {} for universe {}", getName(), taskParams().getUniverseUUID());
      UniverseUpdater updater =
          universe -> {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            // If this universe is not being updated, fail the request.
            if (!universeDetails.updateInProgress) {
              String msg =
                  String.format("Universe %s is not being updated", taskParams().getUniverseUUID());
              log.error(msg);
              throw new RuntimeException(msg);
            }
            taskParams().fieldModifier.accept(universe);
            universe.setUniverseDetails(universeDetails);
          };
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg =
          String.format(
              "Task %s failed for universe %s - %s",
              getName(), taskParams().getUniverseUUID(), e.getMessage());
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
