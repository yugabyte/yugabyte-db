// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;


import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.UniverseDetails;

public class UniverseUpdateSucceeded extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseUpdateSucceeded.class);

  // Parameters for marking universe update as a success.
  public static class Params implements ITaskParams {
    // The universe against which this node's details should be saved.
    public UUID universeUUID;
  }

  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = params;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());

      // Create the update lambda.
      UniverseUpdater updater = new UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          UniverseDetails universeDetails = universe.getUniverseDetails();
          // If this universe is not being edited, fail the request.
          if (!universeDetails.updateInProgress) {
            LOG.error("UserUniverse " + taskParams().universeUUID + " is not being edited.");
            throw new RuntimeException("UserUniverse " + taskParams().universeUUID +
                " is not being edited");
          }
          // Set the operation success flag.
          universeDetails.updateSucceeded = true;
          universe.setUniverseDetails(universeDetails);
        }
      };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      Universe.saveDetails(taskParams().universeUUID, updater);

    } catch (Exception e) {
      String msg = getName() + " failed with exception "  + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
