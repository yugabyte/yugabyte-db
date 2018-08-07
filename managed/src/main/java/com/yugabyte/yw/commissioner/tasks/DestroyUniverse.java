// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.forms.UniverseTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.RemoveUniverseEntry;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;

public class DestroyUniverse extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(DestroyUniverse.class);

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
    public Boolean isForceDelete;
  }

  public Params params()
  {
    return (Params)taskParams;
  }

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

      // Update the DNS entry for all clusters in this universe.
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        createDnsManipulationTask(DnsManager.DnsCommandType.Delete, params().isForceDelete,
                                  cluster.userIntent.providerType, cluster.userIntent.provider,
                                  cluster.userIntent.universeName)
	    .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
      }

      // Create tasks to destroy the existing nodes.
      createDestroyServerTasks(universe.getNodes(), params().isForceDelete, true)
        .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

      // Create tasks to remove the universe entry from the Universe table.
      createRemoveUniverseEntryTask()
        .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(true /* removeFile */);

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

  public SubTaskGroup createRemoveUniverseEntryTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("RemoveUniverseEntry", executor);
    Params params = new Params();
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    params.customerUUID = params().customerUUID;
    params.isForceDelete = params().isForceDelete;

    // Create the Ansible task to destroy the server.
    RemoveUniverseEntry task = new RemoveUniverseEntry();
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }
}
