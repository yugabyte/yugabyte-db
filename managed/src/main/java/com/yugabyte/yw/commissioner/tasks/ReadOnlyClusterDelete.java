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
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteClusterFromUniverse;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

import javax.inject.Inject;
import java.util.UUID;

// Tracks a read only cluster delete intent from a universe.
@Slf4j
public class ReadOnlyClusterDelete extends UniverseDefinitionTaskBase {

  @Inject
  protected ReadOnlyClusterDelete(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public UUID clusterUUID;
    public Boolean isForceDelete = false;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), params().universeUUID);

    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = null;
      if (params().isForceDelete) {
        universe = forceLockUniverseForUpdate(-1 /* expectedUniverseVersion */);
      } else {
        universe = lockUniverseForUpdate(params().expectedUniverseVersion);
      }

      Cluster cluster = universe.getUniverseDetails().getReadOnlyClusters().get(0);

      // Delete all the read-only cluster nodes.
      createDestroyServerTasks(
              universe.getNodesInCluster(cluster.uuid),
              params().isForceDelete,
              true /* deleteNodeFromDB */)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

      // Remove the cluster entry from the universe db entry.
      createDeleteClusterFromUniverseTask(params().clusterUUID);

      // Remove the async_replicas in the cluster config on master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Remove the DNS entry for this cluster.
      createDnsManipulationTask(
              DnsManager.DnsCommandType.Delete, params().isForceDelete, cluster.userIntent)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }

  /**
   * Creates a task to delete a read only cluster info from the universe and adds the task to the
   * task queue.
   *
   * @param clusterUUID uuid of the read-only cluster to be removed.
   */
  public void createDeleteClusterFromUniverseTask(UUID clusterUUID) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("DeleteClusterFromUniverse", executor);
    DeleteClusterFromUniverse.Params params = new DeleteClusterFromUniverse.Params();
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    params.clusterUUID = clusterUUID;
    // Create the task to delete cluster ifo.
    DeleteClusterFromUniverse task = createTask(DeleteClusterFromUniverse.class);
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }
}
