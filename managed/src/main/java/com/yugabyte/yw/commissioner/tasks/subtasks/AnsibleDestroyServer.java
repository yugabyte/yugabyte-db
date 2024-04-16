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
  protected AnsibleDestroyServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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
    // Flag, indicating OpenTelemetry Collector is installed on the DB node.
    public boolean otelCollectorInstalled = false;
  }

  @Override
  protected AnsibleDestroyServer.Params taskParams() {
    return (AnsibleDestroyServer.Params) taskParams;
  }

  private void removeNodeFromUniverse(final String nodeName) {
    Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (u.getNode(nodeName) == null) {
      log.warn("No node in universe with name {}", nodeName);
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
                "Removing node {} from universe {}", nodeName, taskParams().getUniverseUUID());
          }
        };

    saveUniverseDetails(updater);
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
    if (nodeDetails == null) {
      // Nothing can be done.
      log.warn(
          "Node {} is not found in the universe {}",
          taskParams().nodeName,
          universe.getUniverseUUID());
      return;
    }
    boolean cleanupFailed = false;

    // Execute the ansible command.
    try {
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Destroy, taskParams())
          .processErrors();
    } catch (Exception e) {
      if (!taskParams().isForceDelete) {
        throw e;
      } else {
        cleanupFailed = true;
        log.debug(
            "Ignoring error deleting instance {} due to isForceDelete being set.",
            taskParams().nodeName,
            e);
      }
    }

    UserIntent userIntent =
        universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid).userIntent;

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

    try {
      deleteNodeAgent(nodeDetails);
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
        && nodeDetails.state != NodeDetails.NodeState.Decommissioned) {
      // Free up the node.
      try {
        NodeInstance providerNode = NodeInstance.getByName(taskParams().nodeName);
        if (cleanupFailed) {
          log.info(
              "Failed to clean node instance {}. Setting to decommissioned state",
              taskParams().nodeName);
          providerNode.setToFailedCleanup(universe, nodeDetails);
        } else {
          providerNode.clearNodeDetails();
          log.info("Marked node instance {} as available", taskParams().nodeName);
        }
      } catch (Exception e) {
        if (!taskParams().isForceDelete) {
          throw e;
        }
      }
    }
    // Update the node state to Terminated to mark that instance has been terminated. This is a
    // short-lived state as either the node is deleted or the state is changed to Decommissioned.
    setNodeState(NodeDetails.NodeState.Terminated);
    if (taskParams().deleteNode) {
      removeNodeFromUniverse(taskParams().nodeName);
    }
  }
}
