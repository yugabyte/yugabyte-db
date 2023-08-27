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
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AnsibleDestroyServer extends NodeTaskBase {

  @Inject
  protected AnsibleDestroyServer(
      BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  public static class Params extends NodeTaskParams {
    // Flag to be set where errors from ansible will be ignored.
    public boolean isForceDelete;
    // Flag to track if node info should be deleted from universe db.
    public boolean deleteNode = true;
    // Flag to delete root volumes which are not auto-deleted on instance termination.
    public boolean deleteRootVolumes = false;
    // IP of node to be deleted.
    public String nodeIP = null;
  }

  @Override
  protected AnsibleDestroyServer.Params taskParams() {
    return (AnsibleDestroyServer.Params) taskParams;
  }

  private void removeNodeFromUniverse(final String nodeName) {
    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (u.getNode(nodeName) == null) {
      log.error("No node in universe with name " + nodeName);
      return;
    }
    // Persist the desired node information into the DB.
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.removeNode(nodeName);
            log.debug(
                "Removing node " + nodeName + " from universe " + taskParams().getUniverseUUID());
          }
        };

    saveUniverseDetails(updater);
  }

  @Override
  public void run() {
    // Execute the ansible command.
    try {
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Destroy, taskParams())
          .processErrors();
    } catch (Exception e) {
      if (!taskParams().isForceDelete) {
        throw e;
      } else {
        log.debug(
            "Ignoring error deleting instance {} due to isForceDelete being set.",
            taskParams().nodeName,
            e);
      }
    }

    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UserIntent userIntent =
        u.getUniverseDetails()
            .getClusterByUuid(u.getNode(taskParams().nodeName).placementUuid)
            .userIntent;

    if (taskParams().deleteRootVolumes
        && !userIntent.providerType.equals(Common.CloudType.onprem)) {
      try {
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Delete_Root_Volumes, taskParams())
            .processErrors();
      } catch (Exception e) {
        if (!taskParams().isForceDelete) {
          throw e;
        } else {
          log.debug(
              "Ignoring error deleting volumes for {} due to isForceDelete being set.",
              taskParams().nodeName,
              e);
        }
      }
    }

    NodeDetails univNodeDetails = u.getNode(taskParams().nodeName);

    try {
      deleteNodeAgent(univNodeDetails);
    } catch (Exception e) {
      if (!taskParams().isForceDelete) {
        throw e;
      } else {
        log.debug(
            "Ignoring error deleting node agent {} due to isForceDelete being set.",
            taskParams().nodeName,
            e);
      }
    }

    if (userIntent.providerType.equals(Common.CloudType.onprem)
        && univNodeDetails.state != NodeDetails.NodeState.Decommissioned) {
      // Free up the node.
      try {
        NodeInstance providerNode = NodeInstance.getByName(taskParams().nodeName);
        providerNode.clearNodeDetails();
      } catch (Exception e) {
        if (!taskParams().isForceDelete) {
          throw e;
        }
      }
      log.info("Marked node instance {} as available", taskParams().nodeName);
    }

    if (taskParams().deleteNode) {
      // Update the node state to removed. Even though we remove the node below, this will
      // help tracking state for any nodes stuck in limbo.
      setNodeState(NodeDetails.NodeState.Terminated);

      removeNodeFromUniverse(taskParams().nodeName);
    }
  }
}
