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
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.Common;
import org.yb.client.YBClient;

@Slf4j
public class CreateUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected CreateUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  private String ysqlPassword;
  private String ycqlPassword;
  private String ysqlCurrentPassword = Util.DEFAULT_YSQL_PASSWORD;
  private String ysqlUsername = Util.DEFAULT_YSQL_USERNAME;
  private String ycqlCurrentPassword = Util.DEFAULT_YCQL_PASSWORD;
  private String ycqlUsername = Util.DEFAULT_YCQL_USERNAME;
  private String ysqlDb = Util.YUGABYTE_DB;

  @Override
  public void run() {
    log.info("Started {} task.", getName());
    try {
      // Verify the task params.
      verifyParams(UniverseOpType.CREATE);

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Set all the in-memory node names.
      setNodeNames(UniverseOpType.CREATE, universe);

      // Select master nodes.
      selectMasters();

      if (taskParams().getPrimaryCluster().userIntent.enableYCQL
          && taskParams().getPrimaryCluster().userIntent.enableYCQLAuth) {
        ycqlPassword = taskParams().getPrimaryCluster().userIntent.ycqlPassword;
        String ycqlPassLength = ((Integer) ycqlPassword.length()).toString();
        String ycqlRegex = "(.)" + "{" + ycqlPassLength + "}";
        taskParams().getPrimaryCluster().userIntent.ycqlPassword =
            taskParams()
                .getPrimaryCluster()
                .userIntent
                .ycqlPassword
                .replaceAll(ycqlRegex, "REDACTED");
      }
      if (taskParams().getPrimaryCluster().userIntent.enableYSQL
          && taskParams().getPrimaryCluster().userIntent.enableYSQLAuth) {
        ysqlPassword = taskParams().getPrimaryCluster().userIntent.ysqlPassword;
        String ysqlPassLength = ((Integer) ysqlPassword.length()).toString();
        String ysqlRegex = "(.)" + "{" + ysqlPassLength + "}";
        taskParams().getPrimaryCluster().userIntent.ysqlPassword =
            taskParams()
                .getPrimaryCluster()
                .userIntent
                .ysqlPassword
                .replaceAll(ysqlRegex, "REDACTED");
      }

      if (taskParams().firstTry) {
        // Update the user intent.
        universe = writeUserIntentToUniverse();
        updateOnPremNodeUuids(universe);
      }

      // Update the universe to the latest state and
      // check if the nodes already exist in the cloud provider, if so,
      // fail the universe creation.
      universe = Universe.getOrBadRequest(universe.universeUUID);
      checkIfNodesExist(universe);
      Cluster primaryCluster = taskParams().getPrimaryCluster();

      performUniversePreflightChecks(universe, x -> true);

      // Create the required number of nodes in the appropriate locations.
      createCreateServerTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      createServerInfoTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Provision the required number of nodes in the appropriate locations.
      // force reuse host since part of create universe flow
      createSetupServerTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Configures and deploys software on all the nodes (masters and tservers).
      createConfigureServerTasks(taskParams().nodeDetailsSet, false /* isShell */)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);
      // Override master flags (on primary cluster) and tserver flags as necessary.
      createGFlagsOverrideTasks(primaryNodes, ServerType.MASTER);

      // Set default gflags
      addDefaultGFlags(primaryCluster.userIntent);
      createGFlagsOverrideTasks(taskParams().nodeDetailsSet, ServerType.TSERVER);

      // Get the new masters from the node list.
      Set<NodeDetails> newMasters = PlacementInfoUtil.getMastersToProvision(primaryNodes);

      // Creates the YB cluster by starting the masters in the create mode.
      createStartMasterTasks(newMasters).setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for new masters to be responsive.
      createWaitForServersTasks(newMasters, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Start the tservers in the clusters.
      createStartTServersTasks(taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(taskParams().nodeDetailsSet, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Set the node state to live.
      createSetNodeStateTasks(taskParams().nodeDetailsSet, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for a Master Leader to be elected.
      createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Persist the placement info into the YB master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Manage encryption at rest
      SubTaskGroup manageEncryptionKeyTask = createManageEncryptionAtRestTask();
      if (manageEncryptionKeyTask != null) {
        manageEncryptionKeyTask.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Wait for a master leader to hear from all the tservers.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      if (primaryCluster.userIntent.enableYEDIS) {
        // Create a simple redis table.
        createTableTask(Common.TableType.REDIS_TABLE_TYPE, YBClient.REDIS_DEFAULT_TABLE_NAME, null)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Update the DNS entry for all the nodes once, using the primary cluster type.
      createDnsManipulationTask(DnsManager.DnsCommandType.Create, false, primaryCluster.userIntent)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Create alert definitions.
      createUnivCreateAlertDefinitionsTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Change admin password for Admin user, as specified.
      if ((primaryCluster.userIntent.enableYSQL && primaryCluster.userIntent.enableYSQLAuth)
          || (primaryCluster.userIntent.enableYCQL && primaryCluster.userIntent.enableYCQLAuth)) {
        createChangeAdminPasswordTask(
                primaryCluster,
                ysqlPassword,
                ysqlCurrentPassword,
                ysqlUsername,
                ysqlDb,
                ycqlPassword,
                ycqlCurrentPassword,
                ycqlUsername)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }

  private void checkIfNodesExist(Universe universe) {
    String errMsg;
    for (NodeDetails node : universe.getNodes()) {
      if (node.placementUuid == null) {
        errMsg = String.format("Node %s does not have placement.", node.nodeName);
        throw new RuntimeException(errMsg);
      }
      Cluster cluster = universe.getCluster(node.placementUuid);
      if (!cluster.userIntent.providerType.equals(CloudType.onprem)) {
        NodeTaskParams nodeParams = new NodeTaskParams();
        nodeParams.universeUUID = universe.universeUUID;
        nodeParams.expectedUniverseVersion = universe.version;
        nodeParams.nodeName = node.nodeName;
        nodeParams.azUuid = node.azUuid;
        nodeParams.placementUuid = node.placementUuid;
        if (instanceExists(nodeParams)) {
          errMsg =
              String.format("Node %s already exist. Pick different universe name.", node.nodeName);
          throw new RuntimeException(errMsg);
        }
      }
    }
  }
}
