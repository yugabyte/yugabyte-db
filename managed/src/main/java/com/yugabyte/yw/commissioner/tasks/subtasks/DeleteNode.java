// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import java.util.Set;

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
          NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
          if (nodeDetails == null) {
            throw new RuntimeException("No node in universe with name " + taskParams().nodeName);
          }
          Cluster cluster = universeDetails.getClusterByUuid(nodeDetails.placementUuid);

          if (isNodeRemovable(taskParams().nodeName, universeDetails.nodeDetailsSet)) {
            // Remove Node from universe node detail set
            removeNodeByName(taskParams().nodeName, universeDetails.nodeDetailsSet);
            // Update Placement Info to reflect new nodeDetailSet
            Set<NodeDetails> clusterNodes = universeDetails.getNodesInCluster(cluster.uuid);
            updatePlacementInfo(clusterNodes, cluster.placementInfo);

            // Update userIntent to reflect new numNodes
            cluster.userIntent.numNodes = universeDetails.nodeDetailsSet.size();
            // If OnPrem Free up the node.
            if (cluster.userIntent.providerType.equals(Common.CloudType.onprem)) {
              NodeInstance node = NodeInstance.getByName(taskParams().nodeName);
              node.clearNodeDetails();
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
