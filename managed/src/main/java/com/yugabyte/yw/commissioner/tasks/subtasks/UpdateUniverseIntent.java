/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateUniverseIntent extends UniverseTaskBase {

  @Inject
  protected UpdateUniverseIntent(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public boolean updatePlacementInfo = false;
    public List<NodeDetails> clusterNodeDetails;
  }

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
          universe -> {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            // If this universe is not being updated, fail the request.
            if (!universeDetails.updateInProgress) {
              String msg =
                  "UserUniverse " + taskParams().getUniverseUUID() + " is not being updated.";
              log.error(msg);
              throw new RuntimeException(msg);
            }
            UniverseDefinitionTaskParams.Cluster cluster = taskParams().clusters.get(0);

            if (taskParams().updatePlacementInfo) {
              List<NodeDetails> clusterNodeDetails = taskParams().clusterNodeDetails;
              PlacementInfoUtil.updatePlacementInfo(clusterNodeDetails, cluster.placementInfo);

              cluster.userIntent.numNodes = clusterNodeDetails.size();
              log.info(
                  "Setting taskParams cluster placement info to {} and numNodes to {} for universe"
                      + " {} cluster {}",
                  cluster.placementInfo.toString(),
                  cluster.userIntent.numNodes,
                  taskParams().getUniverseUUID(),
                  cluster.uuid);
            }
            universe
                .getUniverseDetails()
                .upsertCluster(cluster.userIntent, cluster.placementInfo, cluster.uuid);
            universe.setUniverseDetails(universeDetails);
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
