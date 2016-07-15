// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;


import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.AbstractTaskBase;
import forms.commissioner.TaskParamsBase;
import models.commissioner.InstanceInfo;
import models.commissioner.InstanceInfo.InstanceDetails;
import models.commissioner.InstanceInfo.UniverseUpdater;

public class UniverseUpdateSucceeded extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseUpdateSucceeded.class);

  // Parameters for instance info update task.
  public static class Params extends TaskParamsBase {}

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());

      // Create the update lambda.
      UniverseUpdater updater = new UniverseUpdater() {
        @Override
        public void run(InstanceInfo universe) {
          InstanceDetails instanceDetails = universe.universeDetails;
          // If this universe is not being edited, fail the request.
          if (!instanceDetails.updateInProgress) {
            LOG.error("Universe " + taskParams.instanceUUID + " is not being edited.");
            throw new RuntimeException("Universe " + taskParams.instanceUUID +
                " is not being edited");
          }
          // Set the operation success flag.
          instanceDetails.updateSucceeded = true;
        }
      };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      InstanceInfo.save(taskParams.instanceUUID, updater);

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
