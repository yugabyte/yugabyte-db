// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import static com.yugabyte.yw.common.PlacementInfoUtil.*;

public class DeleteNode extends NodeTaskBase {

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());
      // Create the update lambda.
      Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          // If this universe is not being edited, fail the request.
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster primaryCluster = universeDetails.retrievePrimaryCluster();
          if (isNodeRemovable(taskParams().nodeName, universeDetails.nodeDetailsSet)) {
            // Remove Node from universe node detail set
            removeNodeByName(taskParams().nodeName, universeDetails.nodeDetailsSet);
            // Update Placement Info to reflect new nodeDetailSet
            updatePlacementInfo(universeDetails.nodeDetailsSet, primaryCluster.placementInfo);
            // Update userIntent to reflect new numNodes
            primaryCluster.userIntent.numNodes = universeDetails.nodeDetailsSet.size();
            // If OnPrem Free up the node.
            if (taskParams().cloud == Common.CloudType.onprem) {
              NodeInstance node = NodeInstance.getByName(taskParams().nodeName);
              node.inUse = false;
              node.save();
            }
            universe.setUniverseDetails(universeDetails);
          }
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
