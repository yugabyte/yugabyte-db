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
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.AllArgsConstructor;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class CreateUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected CreateUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // In-memory password store for ysqlPassword and ycqlPassword.
  private static final Cache<UUID, AuthPasswords> passwordStore =
      CacheBuilder.newBuilder().expireAfterAccess(2, TimeUnit.DAYS).maximumSize(1000).build();

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

      Cluster primaryCluster = taskParams().getPrimaryCluster();
      boolean isYCQLAuthEnabled =
          primaryCluster.userIntent.enableYCQL && primaryCluster.userIntent.enableYCQLAuth;
      boolean isYSQLAuthEnabled =
          primaryCluster.userIntent.enableYSQL && primaryCluster.userIntent.enableYSQLAuth;

      // Store the passwords in the temporary variables first.
      // DB does not store the actual passwords.
      if (isYCQLAuthEnabled || isYSQLAuthEnabled) {
        if (isFirstTryForTask(taskParams())) {
          if (isYCQLAuthEnabled) {
            ycqlPassword = primaryCluster.userIntent.ycqlPassword;
          }
          if (isYSQLAuthEnabled) {
            ysqlPassword = primaryCluster.userIntent.ysqlPassword;
          }
        }
      }

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      // It returns the latest state of the Universe after saving.
      Universe universe =
          lockUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> {
                if (isFirstTryForTask(taskParams())) {
                  // Fetch the task params from the DB to start from fresh on retry.
                  // Otherwise, some operations like name assignment can fail.
                  fetchTaskDetailsFromDB();
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

      if (isYCQLAuthEnabled || isYSQLAuthEnabled) {
        if (isFirstTryForTask(taskParams())) {
          if (isYCQLAuthEnabled) {
            taskParams().getPrimaryCluster().userIntent.ycqlPassword =
                RedactingService.redactString(ycqlPassword);
          }
          if (isYSQLAuthEnabled) {
            taskParams().getPrimaryCluster().userIntent.ysqlPassword =
                RedactingService.redactString(ysqlPassword);
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

      // Create preflight node check tasks for on-prem nodes.
      createPreflightNodeCheckTasks(universe, taskParams().clusters);

      // Provision the nodes.
      // State checking is enabled because the subtasks are not idempotent.
      createProvisionNodeTasks(
          universe,
          taskParams().nodeDetailsSet,
          false /* ignore node status check */,
          null /* setup server param customizer */,
          null /* install software param customizer */,
          gFlagsParams -> {
            gFlagsParams.masterJoinExistingCluster = false;
          });

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

      createConfigureUniverseTasks(primaryCluster, newMasters);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
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
