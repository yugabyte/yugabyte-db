// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateUniverseYbcDetails extends UniverseTaskBase {

  @Inject
  protected UpdateUniverseYbcDetails(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {}

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      // Create the update lambda.
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              // If this universe is not being edited, fail the request.
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              if (!universeDetails.updateInProgress) {
                String errMsg =
                    "UserUniverse " + taskParams().getUniverseUUID() + " is not being edited.";
                log.error(errMsg);
                throw new RuntimeException(errMsg);
              }
              universeDetails.setYbcSoftwareVersion(taskParams().getYbcSoftwareVersion());
              universeDetails.setEnableYbc(taskParams().isEnableYbc());
              universeDetails.setYbcInstalled(taskParams().isYbcInstalled());
              universe.setUniverseDetails(universeDetails);
            }
          };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
