// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PersistSystemdUpgrade extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(PersistSystemdUpgrade.class);

  @Inject
  public PersistSystemdUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public Boolean useSystemd;
  }

  protected PersistSystemdUpgrade.Params taskParams() {
    return (PersistSystemdUpgrade.Params) taskParams;
  }

  @Override
  public String getName() {
    String ret =
        super.getName()
            + "("
            + taskParams().getUniverseUUID()
            + ", useSystemd: "
            + taskParams().useSystemd
            + ")";
    return ret;
  }

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());
      // Create the update lambda.
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              // Update useSystemd to true for all clusters in the universe.
              universeDetails.getPrimaryCluster().userIntent.useSystemd = true;
              universeDetails
                  .getReadOnlyClusters()
                  .forEach((readReplica) -> readReplica.userIntent.useSystemd = true);
              universe.setUniverseDetails(universeDetails);
            }
          };
      // Perform the update.
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
