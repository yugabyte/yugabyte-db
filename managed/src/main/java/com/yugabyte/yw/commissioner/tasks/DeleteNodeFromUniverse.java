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

import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
public class DeleteNodeFromUniverse extends UniverseTaskBase {

  @Inject
  protected DeleteNodeFromUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    try {
      checkUniverseVersion();
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      log.info(
          "Delete Node with name {} from universe {}",
          taskParams().nodeName,
          taskParams().getUniverseUUID());

      preTaskActions();

      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg =
            String.format(
                "No node %s is found in universe %s", taskParams().nodeName, universe.getName());
        log.error(msg);
        throw new RuntimeException(msg);
      }

      // This same check is also done in DeleteNode subtask.
      if (!currentNode.isRemovable()) {
        String msg =
            String.format(
                "Node %s with state %s is not removable from universe %s",
                currentNode.nodeName, currentNode.state, universe.getName());
        log.error(msg);
        throw new IllegalStateException(msg);
      }

      UserIntent userIntent =
          universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid).userIntent;
      boolean isOnprem = CloudType.onprem.equals(userIntent.providerType);

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;

      // DELETE action is allowed on InstanceCreated, SoftwareInstalled states etc.
      // A failed AddNodeToUniverse after ReleaseInstanceFromUniverse can leave instances behind.
      if (instanceExists(taskParams()) || isOnprem) {
        Collection<NodeDetails> currentNodeDetails = Sets.newHashSet(currentNode);
        // Create tasks to terminate that instance.
        // If destroy of the instance fails for some reason, this task can always be retried
        // because there is no change in the node state that can make this task move to one of
        // the disallowed actions.
        createDestroyServerTasks(
                universe,
                currentNodeDetails,
                true /* isForceDelete */,
                false /* deleteNode */,
                true /* deleteRootVolumes */)
            .setSubTaskGroupType(SubTaskGroupType.DeletingNode);
      }

      createDeleteNodeFromUniverseTask(taskParams().nodeName)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeletingNode);
      // Set Universe Update Success to true, if delete node succeeds for now.
      // Should probably roll back to a previous success state instead of setting to true
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeletingNode);
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error(String.format("Error executing task %s, error %s", getName(), t.getMessage()), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
