/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PauseUniverse extends UniverseTaskBase {

  @Inject
  protected PauseUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      // Verify the task params.
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      if (UniverseDefinitionTaskParams.hasEphemeralStorage(universe.getUniverseDetails())) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot pause universe with ephemeral storage");
      }
    }
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockAndFreezeUniverseForUpdate(-1, null /* Txn callback */);
      if (universe.getUniverseDetails().universePaused) {
        String msg =
            "Unable to pause universe \"" + universe.getName() + "\" as it is already paused.";
        log.error(msg);
        throw new RuntimeException(msg);
      }

      createPauseUniverseTasks(universe, params().customerUUID);

      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.PauseUniverse);

      getRunnableTask().runSubTasks();

    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
