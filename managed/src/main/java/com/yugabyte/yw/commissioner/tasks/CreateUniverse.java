// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.Common;
import org.yb.client.YBClient;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class CreateUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateUniverse.class);

  // Set initial number of tablets per tablet servers.
  public static int NUM_TABLETS_PER_TSERVER = 8;

  @Override
  public void run() {
    LOG.info("Started {} task.", getName());
    try {
      // Verify the task params.
      verifyParams();

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Ensure there are no masters that were selected, pick them here.
      if (PlacementInfoUtil.getNumMasters(taskParams().nodeDetailsSet) > 0) {
        throw new IllegalStateException("Should not have any masters before create task run.");
      }
      PlacementInfoUtil.selectMasters(taskParams().nodeDetailsSet,
                                      taskParams().retrievePrimaryCluster().userIntent.replicationFactor);

      // Update the user intent.
      writeUserIntentToUniverse();

      // Set the correct node names as they are finalized now. This is done just in case the user
      // changes the universe name before submitting.
      updateNodeNames();

      // Create the required number of nodes in the appropriate locations.
      createSetupServerTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      createServerInfoTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(taskParams().nodeDetailsSet, false /* isShell */)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Override master flags if necessary
      SubTaskGroup subTaskGroup = createGFlagsOverrideTasks(taskParams().nodeDetailsSet, ServerType.MASTER);
      if (subTaskGroup != null) {
        subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      }
      // Override tserver flags if necessary
      subTaskGroup = createGFlagsOverrideTasks(taskParams().nodeDetailsSet, ServerType.TSERVER);
      if (subTaskGroup != null) {
        subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      }

      // Get the new masters from the node list.
      Set<NodeDetails> newMasters = PlacementInfoUtil.getMastersToProvision(
          taskParams().nodeDetailsSet);

      // Creates the YB cluster by starting the masters in the create mode.
      createStartMasterTasks(newMasters, false /* isShell */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for new masters to be responsive.
      createWaitForServersTasks(newMasters, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Start the tservers in the clusters.
      createStartTServersTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(taskParams().nodeDetailsSet, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Set the node state to running.
      createSetNodeStateTasks(taskParams().nodeDetailsSet, NodeDetails.NodeState.Running)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for a Master Leader to be elected.
      createWaitForMasterLeaderTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Persist the placement info into the YB master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for a master leader to hear from atleast replication factor number of tservers.
      createWaitForTServerHeartBeatsTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the swamper target file (implicitly calls SubTaskGroupType)
      createSwamperTargetUpdateTask(false /* removeFile */, SubTaskGroupType.ConfigureUniverse);

      // Create a simple redis table.
      int numTServers = PlacementInfoUtil.getTserversToProvision(taskParams().nodeDetailsSet).size();
      int numTablets = NUM_TABLETS_PER_TSERVER * numTServers;
      createTableTask(Common.TableType.REDIS_TABLE_TYPE, YBClient.REDIS_DEFAULT_TABLE_NAME,
                      numTablets, null)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
