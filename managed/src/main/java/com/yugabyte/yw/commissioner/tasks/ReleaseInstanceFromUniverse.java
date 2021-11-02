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
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;
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
  public void run() {
    log.info(
        "Started {} task for node {} in univ uuid={}",
        getName(),
        taskParams().nodeName,
        taskParams().universeUUID);
    NodeDetails currentNode = null;
    boolean hitException = false;
    try {
      checkUniverseVersion();
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " found in universe " + universe.name;
        log.error(msg);
        throw new RuntimeException(msg);
      }

      if (currentNode.state != NodeState.Removed) {
        String msg =
            "Node "
                + taskParams().nodeName
                + " is not on removed or added state, but "
                + "is in "
                + currentNode.state
                + ", so cannot be released.";
        log.error(msg);
        throw new RuntimeException(msg);
      }

      preTaskActions();

      // Update Node State to BeingDecommissioned.
      createSetNodeStateTask(currentNode, NodeState.BeingDecommissioned)
          .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;

      // Wait for Master Leader before doing Master operations, like blacklisting.
      createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
      // Create a task for removal of this server from blacklist on master leader.
      createModifyBlackListTask(
              Arrays.asList(currentNode), false /* isAdd */, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);

      UserIntent userIntent =
          universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid).userIntent;
      boolean isOnprem = userIntent.providerType.equals(CloudType.onprem);
      if (instanceExists(taskParams()) || isOnprem) {
        Collection<NodeDetails> currentNodeDetails = new HashSet<>(Arrays.asList(currentNode));
        if (isOnprem) {
          // Stop master and tservers.
          createStopServerTasks(currentNodeDetails, "master", true /* isForceDelete */)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          createStopServerTasks(currentNodeDetails, "tserver", true /* isForceDelete */)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }

        // Create tasks to terminate that instance. Force delete and ignore errors.
        createDestroyServerTasks(
                currentNodeDetails, true /* isForceDelete */, false /* deleteNode */)
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
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      hitException = true;
      throw t;
    } finally {
      // Reset the state, on any failure, so that the actions can be retried.
      if (currentNode != null && hitException) {
        setNodeState(taskParams().nodeName, currentNode.state);
      }

      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
