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

import static com.yugabyte.yw.forms.UniverseTaskParams.isFirstTryForTask;

import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
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
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.AllArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;
import org.yb.client.YBClient;

@Slf4j
public class CreateUniverse extends UniverseDefinitionTaskBase {

  private static final String MIN_WRITE_READ_TABLE_CREATION_RELEASE = "2.6.0.0";

  @Inject
  protected CreateUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // In-memory password store for ysqlPassword and ycqlPassword.
  private static final Cache<UUID, AuthPasswords> passwordStore =
      CacheBuilder.newBuilder().expireAfterAccess(2, TimeUnit.DAYS).maximumSize(1000).build();

  private String ysqlPassword;
  private String ycqlPassword;
  private String ysqlCurrentPassword = Util.DEFAULT_YSQL_PASSWORD;
  private String ysqlUsername = Util.DEFAULT_YSQL_USERNAME;
  private String ycqlCurrentPassword = Util.DEFAULT_YCQL_PASSWORD;
  private String ycqlUsername = Util.DEFAULT_YCQL_USERNAME;
  private String ysqlDb = Util.YUGABYTE_DB;

  @AllArgsConstructor
  private static class AuthPasswords {
    public String ycqlPassword;
    public String ysqlPassword;
  }

  // CreateUniverse can be retried, so all tasks within should be idempotent. For an example of how
  // to achieve idempotence or to make retries more performant, see the createProvisionNodeTasks
  // pattern below
  @Override
  public void run() {
    log.info("Started {} task.", getName());
    try {
      if (isFirstTryForTask(taskParams())) {
        // Verify the task params.
        verifyParams(UniverseOpType.CREATE);
      }

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      // It returns the latest state of the Universe after saving.
      Universe universe =
          lockUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> {
                if (isFirstTryForTask(taskParams())) {
                  // Set all the in-memory node names.
                  setNodeNames(u);
                  // Select master nodes and apply isMaster flags immediately.
                  selectAndApplyMasters();
                  // Set non on-prem node UUIDs.
                  setCloudNodeUuids(u);
                  // Update on-prem node UUIDs.
                  updateOnPremNodeUuidsOnTaskParams();
                  // Set the prepared data to universe in-memory.
                  setUserIntentToUniverse(u, taskParams(), false);
                  // There is a rare possibility that this succeeds and
                  // saving the Universe fails. It is ok because the retry
                  // will just fail.
                  updateTaskDetailsInDB(taskParams());
                }
              });

      Cluster primaryCluster = taskParams().getPrimaryCluster();
      boolean isYCQLAuthEnabled =
          primaryCluster.userIntent.enableYCQL && primaryCluster.userIntent.enableYCQLAuth;
      boolean isYSQLAuthEnabled =
          primaryCluster.userIntent.enableYSQL && primaryCluster.userIntent.enableYSQLAuth;

      if (isYCQLAuthEnabled || isYSQLAuthEnabled) {
        if (isFirstTryForTask(taskParams())) {
          if (isYCQLAuthEnabled) {
            ycqlPassword = taskParams().getPrimaryCluster().userIntent.ycqlPassword;
            taskParams().getPrimaryCluster().userIntent.ycqlPassword =
                Util.redactString(ycqlPassword);
          }
          if (isYSQLAuthEnabled) {
            ysqlPassword = taskParams().getPrimaryCluster().userIntent.ysqlPassword;
            taskParams().getPrimaryCluster().userIntent.ysqlPassword =
                Util.redactString(ysqlPassword);
          }
          log.debug("Storing passwords in memory");
          passwordStore.put(universe.universeUUID, new AuthPasswords(ycqlPassword, ysqlPassword));
        } else {
          log.debug("Reading password for {}", universe.universeUUID);
          // Read from the in-memory store on retry.
          AuthPasswords passwords = passwordStore.getIfPresent(universe.universeUUID);
          if (passwords == null) {
            throw new RuntimeException(
                "Auth passwords are not found. Platform might have restarted"
                    + " or task might have expired");
          }
          ycqlPassword = passwords.ycqlPassword;
          ysqlPassword = passwords.ysqlPassword;
        }
      }

      // TODO this can be moved to subtasks.
      validateNodeExistence(universe);

      performUniversePreflightChecks(universe.getUniverseDetails().clusters);

      // Provision the nodes.
      // State checking is enabled because the subtasks are not idempotent.
      createProvisionNodeTasks(
          universe,
          taskParams().nodeDetailsSet,
          false /* isShell */,
          false /* ignore node status check */);

      Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);

      // Get the new masters from the node list.
      Set<NodeDetails> newMasters = PlacementInfoUtil.getMastersToProvision(primaryNodes);

      // Start masters.
      createStartMasterProcessTasks(newMasters);

      // Start tservers on all nodes.
      createStartTserverProcessTasks(taskParams().nodeDetailsSet);

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
        createTableTask(TableType.REDIS_TABLE_TYPE, YBClient.REDIS_DEFAULT_TABLE_NAME, null)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      if (primaryCluster.userIntent.enableYSQL
          && CommonUtils.isReleaseEqualOrAfter(
              MIN_WRITE_READ_TABLE_CREATION_RELEASE, primaryCluster.userIntent.ybSoftwareVersion)) {
        // Create read-write test table
        List<NodeDetails> tserverLiveNodes =
            universe
                .getUniverseDetails()
                .getNodesInCluster(primaryCluster.uuid)
                .stream()
                .filter(nodeDetails -> nodeDetails.isTserver)
                .collect(Collectors.toList());
        createReadWriteTestTableTask(tserverLiveNodes.size(), true)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Update the DNS entry for all the nodes once, using the primary cluster type.
      createDnsManipulationTask(DnsManager.DnsCommandType.Create, false, primaryCluster.userIntent)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Create alert definitions.
      createUnivCreateAlertDefinitionsTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Change admin password for Admin user, as specified.
      if (isYCQLAuthEnabled || isYSQLAuthEnabled) {
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
      Universe universe = unlockUniverseForUpdate();
      if (universe != null && universe.getUniverseDetails().updateSucceeded) {
        log.debug("Removing passwords for {}", universe.universeUUID);
        passwordStore.invalidate(universe.universeUUID);
      }
    }
    log.info("Finished {} task.", getName());
  }

  private void validateNodeExistence(Universe universe) {
    for (NodeDetails node : universe.getNodes()) {
      if (node.placementUuid == null) {
        String errMsg = String.format("Node %s does not have placement.", node.nodeName);
        throw new RuntimeException(errMsg);
      }
      Cluster cluster = universe.getCluster(node.placementUuid);
      if (cluster.userIntent.providerType.equals(CloudType.onprem)) {
        continue;
      }
      Map<String, String> expectedTags = new HashMap<>();
      expectedTags.put("universe_uuid", universe.universeUUID.toString());
      if (node.nodeUuid != null) {
        expectedTags.put("node_uuid", node.nodeUuid.toString());
      }
      NodeTaskParams nodeParams = new NodeTaskParams();
      nodeParams.universeUUID = universe.universeUUID;
      nodeParams.expectedUniverseVersion = universe.version;
      nodeParams.nodeName = node.nodeName;
      nodeParams.nodeUuid = node.nodeUuid;
      nodeParams.azUuid = node.azUuid;
      nodeParams.placementUuid = node.placementUuid;
      Optional<Boolean> optional = instanceExists(nodeParams, expectedTags);
      if (optional.isPresent() && !optional.get()) {
        String errMsg =
            String.format("Node %s already exist. Pick different universe name.", node.nodeName);
        throw new RuntimeException(errMsg);
      }
    }
  }
}
