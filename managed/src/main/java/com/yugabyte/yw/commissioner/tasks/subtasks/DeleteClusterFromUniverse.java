/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteClusterFromUniverse extends UniverseTaskBase {

  @Inject
  protected DeleteClusterFromUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    // The cluster we are removing from above universe.
    public UUID clusterUUID;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "'("
        + taskParams().getUniverseUUID()
        + " "
        + taskParams().clusterUUID
        + ")'";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      // Create the update lambda.
      Universe.UniverseUpdater updater =
          new Universe.UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.deleteCluster(taskParams().clusterUUID);
              universeDetails.nodeDetailsSet.removeIf(
                  n -> n.isInPlacement(taskParams().clusterUUID));
              universe.setUniverseDetails(universeDetails);
            }
          };
      saveUniverseDetails(updater);
      log.info("Delete cluster {} done.", taskParams().clusterUUID);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
