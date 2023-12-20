/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collection;
import java.util.Collections;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Allows the removal of the instance from a universe. That node is already not part of the
// universe and is in Removed state.
@Slf4j
public class ReleaseInstanceFromUniverse extends UniverseTaskBase {

  @Inject
  protected ReleaseInstanceFromUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    Universe universe = getUniverse();
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    if (currentNode == null) {
      String msg = "No node " + taskParams().nodeName + " found in universe " + universe.getName();
      log.error(msg);
      throw new RuntimeException(msg);
    }

    if (isFirstTry()) {
      currentNode.validateActionOnState(NodeActionType.RELEASE);
    }
  }

  @Override
  public void run() {
    log.info(
        "Started {} task for node {} in univ uuid={}",
        getName(),
        taskParams().nodeName,
        taskParams().universeUUID);
    NodeDetails currentNode = null;
    boolean hitException = false;
    checkUniverseVersion();

    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, null /* Txn callback */);
    try {
      currentNode = universe.getNode(taskParams().nodeName);
      preTaskActions();

      // Update Node State to BeingDecommissioned.
      createSetNodeStateTask(currentNode, NodeState.BeingDecommissioned)
          .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;
      taskParams().nodeUuid = currentNode.nodeUuid;
      Collection<NodeDetails> currentNodeDetails = Collections.singleton(currentNode);
      // Wait for Master Leader before doing Master operations, like blacklisting.
      createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
      // If the node fails in Adding state during ADD action, IP may not be available.
      // Check to make sure that the node IP is available.
      if (Util.getNodeIp(universe, currentNode) != null) {
        // Create a task for removal of this server from blacklist on master leader.
        createModifyBlackListTask(
                currentNodeDetails, false /* isAdd */, false /* isLeaderBlacklist */)
            .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
      }
      UserIntent userIntent =
          universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid).userIntent;
      // Method instanceExists also checks for on-prem.
      if (instanceExists(taskParams())) {
        if (userIntent.providerType == CloudType.onprem) {
          // Stop master and tservers.
          createStopServerTasks(currentNodeDetails, "master", true /* isForceDelete */)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          createStopServerTasks(currentNodeDetails, "tserver", true /* isForceDelete */)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }

        // Set the node states to Removing.
        createSetNodeStateTasks(currentNodeDetails, NodeDetails.NodeState.Terminating)
            .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
        // Create tasks to terminate that instance. Force delete and ignore errors.
        createDestroyServerTasks(
                universe,
                currentNodeDetails,
                true /* isForceDelete */,
                false /* deleteNode */,
                true /* deleteRootVolumes */)
            .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
      }

      // Update Node State to Decommissioned.
      createSetNodeStateTask(currentNode, NodeState.Decommissioned)
          .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, userIntent)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Mark universe task state to success
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      hitException = true;
      throw t;
    } finally {
      try {
        // Reset the state, on any failure, so that the actions can be retried.
        if (currentNode != null && hitException) {
          setNodeState(taskParams().nodeName, currentNode.state);
        }
      } finally {
        // Mark the update of the universe as done. This will allow future edits/updates to the
        // universe to happen.
        unlockUniverseForUpdate();
      }
    }
    log.info("Finished {} task.", getName());
  }
}
