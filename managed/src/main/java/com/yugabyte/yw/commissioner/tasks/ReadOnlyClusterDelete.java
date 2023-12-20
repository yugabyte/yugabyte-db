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
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteClusterFromUniverse;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.UniverseInProgressException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.jsonwebtoken.lang.Collections;
import java.util.Collection;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

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
  protected void validateUniverseState(Universe universe) {
    try {
      super.validateUniverseState(universe);
    } catch (UniverseInProgressException e) {
      if (!params().isForceDelete) {
        throw e;
      }
    }
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), params().universeUUID);

    try {

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = null;
      if (params().isForceDelete) {
        universe = forceLockUniverseForUpdate(-1 /* expectedUniverseVersion */);
      } else {
        universe = lockUniverseForUpdate(params().expectedUniverseVersion);
      }

      List<Cluster> roClusters = universe.getUniverseDetails().getReadOnlyClusters();
      if (Collections.isEmpty(roClusters)) {
        String msg =
            "Unable to delete RO cluster from universe \""
                + universe.name
                + "\" as it doesn't have any RO clusters.";
        log.error(msg);
        throw new RuntimeException(msg);
      }

      preTaskActions();

      // Delete all the read-only cluster nodes.
      Cluster cluster = roClusters.get(0);
      Collection<NodeDetails> nodesToBeRemoved = universe.getNodesInCluster(cluster.uuid);
      // Set the node states to Removing.
      createSetNodeStateTasks(nodesToBeRemoved, NodeDetails.NodeState.Terminating)
          .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
      createDestroyServerTasks(
              universe,
              nodesToBeRemoved,
              params().isForceDelete,
              true /* deleteNodeFromDB */,
              true /* deleteRootVolumes */)
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
      getRunnableTask().runSubTasks();
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
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("DeleteClusterFromUniverse", executor);
    DeleteClusterFromUniverse.Params params = new DeleteClusterFromUniverse.Params();
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    params.clusterUUID = clusterUUID;
    // Create the task to delete cluster ifo.
    DeleteClusterFromUniverse task = createTask(DeleteClusterFromUniverse.class);
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }
}
