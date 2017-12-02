// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;


import java.util.UUID;

import com.yugabyte.yw.forms.AbstractTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;

public class UpdateSoftwareVersion extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpdateSoftwareVersion.class);

  // Parameters for marking universe update as a success.
  public static class Params extends AbstractTaskParams {
    // The universe against which software version should be saved.
    public UUID universeUUID;
    // The software version to which user updated the universe.
    public String softwareVersion;
  }

  protected Params taskParams() {
    return (Params)taskParams;
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
          // If this universe is not being edited, fail the request.
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          if (!universeDetails.updateInProgress) {
            LOG.error("UserUniverse " + taskParams().universeUUID + " is not being edited.");
            throw new RuntimeException("UserUniverse " + taskParams().universeUUID +
                " is not being edited");
          }
          universeDetails.retrievePrimaryCluster().userIntent.ybSoftwareVersion = taskParams().softwareVersion;
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
