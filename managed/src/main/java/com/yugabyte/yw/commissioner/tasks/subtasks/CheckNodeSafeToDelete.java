// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckNodeSafeToDelete extends UniverseTaskBase {

  @Inject
  protected CheckNodeSafeToDelete(BaseTaskDependencies baskTaskDependencies) {
    super(baskTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);

    if (currentNode == null) {
      String msg = "No node " + taskParams().nodeName + " found in universe " + universe.getName();
      log.warn(msg);
      return;
    }

    String nodeIp = Util.getNodeIp(universe, currentNode);
    if (nodeIp == null) {
      log.debug(
          "Skipping {} check as node ip for node: {} is null", getName(), currentNode.nodeName);
      return;
    }

    // Validate there are no tablets assigned to this node.
    if (currentNode.isTserver) {
      checkNoTabletsOnNode(universe, currentNode);
    }

    if (currentNode.isMaster) {
      // Validate that current node's ip is not part of the master quorum.
      boolean isNodeInMasterConfig = nodeInMasterConfig(universe, currentNode);
      log.debug(
          "Node {} has a master in the master config: {}",
          currentNode.getNodeName(),
          isNodeInMasterConfig);
      if (isNodeInMasterConfig) {
        throw new RuntimeException(
            String.format(
                "Could not verify that node %s (IP %s) is not part of the master quorum. Removal of"
                    + " a node that is still part of the master quorum can cause data loss."
                    + " To adjust this check, use the runtime configs %s and %s",
                currentNode.getNodeName(),
                currentNode.cloudInfo.private_ip,
                UniverseConfKeys.clusterMembershipCheckEnabled.getKey(),
                UniverseConfKeys.clusterMembershipCheckTimeout.getKey()));
      }
    }

    log.debug("{} subtask completed successfully", getName());
  }
}
