// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;


import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.AbstractTaskBase;
import forms.commissioner.TaskParamsBase;
import models.commissioner.Universe;
import models.commissioner.Universe.UniverseDetails;
import models.commissioner.Universe.UniverseUpdater;

public class UniverseUpdateSucceeded extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseUpdateSucceeded.class);

  // Parameters for the task.
  public static class Params extends TaskParamsBase {}

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());

      // Create the update lambda.
      UniverseUpdater updater = new UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          UniverseDetails universeDetails = universe.universeDetails;
          // If this universe is not being edited, fail the request.
          if (!universeDetails.updateInProgress) {
            LOG.error("Universe " + taskParams.universeUUID + " is not being edited.");
            throw new RuntimeException("Universe " + taskParams.universeUUID +
                " is not being edited");
          }
          // Set the operation success flag.
          universeDetails.updateSucceeded = true;
        }
      };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      Universe.save(taskParams.universeUUID, updater);

    } catch (Exception e) {
      LOG.warn("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(getName() + " hit error: " , e);
    }
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("name : " + getName() + " " + getTaskDetails().toString());
    return sb.toString();
  }
}
