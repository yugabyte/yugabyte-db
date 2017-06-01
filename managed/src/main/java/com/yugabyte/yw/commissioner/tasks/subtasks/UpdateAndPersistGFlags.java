// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import java.util.HashMap;
import java.util.Map;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class UpdateAndPersistGFlags extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpdateAndPersistGFlags.class);

  // Parameters for setting and persisting universe gflags.
  public static class Params extends UniverseTaskParams {
    public Map<String, String> newGflags;
  }

  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
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
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          // If this universe is not being updated, fail the request.
          if (!universeDetails.updateInProgress) {
            String msg = "UserUniverse " + taskParams().universeUUID + " is not being updated.";
            LOG.error(msg);
            throw new RuntimeException(msg);
          }

          // Update the gflags.
          UserIntent userIntent = universeDetails.userIntent;
          userIntent.gflags.putAll(taskParams().newGflags);
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
