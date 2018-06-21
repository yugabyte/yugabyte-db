// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.UUID;

public class DestroyKubernetesUniverse extends DestroyUniverse {
  public static final Logger LOG = LoggerFactory.getLogger(DestroyKubernetesUniverse.class);

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = null;
      if (params().isForceDelete) {
        universe = forceLockUniverseForUpdate(-1);
      } else {
        universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */);
      }
      UniverseDefinitionTaskParams.UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

      UUID providerUUID = UUID.fromString(userIntent.provider);

      // We need to call Helm Delete first to delete the helm chart and then delete all the
      // volumes that were attached to the pods.
      createDestroyKubernetesTask(providerUUID,
          universe.getUniverseDetails().nodePrefix,
          KubernetesCommandExecutor.CommandType.HELM_DELETE);
      createDestroyKubernetesTask(providerUUID,
          universe.getUniverseDetails().nodePrefix,
          KubernetesCommandExecutor.CommandType.VOLUME_DELETE);

      // Create tasks to remove the universe entry from the Universe table.
      createRemoveUniverseEntryTask()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      // If for any reason destroy fails we would just unlock the universe for update
      try {
        unlockUniverseForUpdate();
      } catch (Throwable t1) {
        // Ignore the error
      }
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    }
    LOG.info("Finished {} task.", getName());
  }

  protected void createDestroyKubernetesTask(UUID providerUUID, String nodePrefix,
                                             KubernetesCommandExecutor.CommandType commandType) {
    SubTaskGroup subTaskGroup = new SubTaskGroup(commandType.getSubTaskGroupName(), executor);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.providerUUID = providerUUID;
    params.commandType = commandType;
    params.nodePrefix = nodePrefix;
    params.universeUUID = taskParams().universeUUID;
    KubernetesCommandExecutor task = new KubernetesCommandExecutor();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RemovingUnusedServers);
  }
}
