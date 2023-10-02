/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.LoadBalancerConfig;
import com.yugabyte.yw.models.helpers.LoadBalancerPlacement;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Map;
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
      if (isFirstTry()) {
        // Verify the task params.
        verifyParams(UniverseOpType.CREATE);
      }

      Cluster primaryCluster = taskParams().getPrimaryCluster();
      boolean isYSQLEnabled = primaryCluster.userIntent.enableYSQL;
      boolean isYCQLAuthEnabled =
          primaryCluster.userIntent.enableYCQL && primaryCluster.userIntent.enableYCQLAuth;
      boolean isYSQLAuthEnabled = isYSQLEnabled && primaryCluster.userIntent.enableYSQLAuth;

      // Store the passwords in the temporary variables first.
      // DB does not store the actual passwords.
      if (isYCQLAuthEnabled || isYSQLAuthEnabled) {
        if (isFirstTry()) {
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
                if (isFirstTry()) {
                  // Fetch the task params from the DB to start from fresh on retry.
                  // Otherwise, some operations like name assignment can fail.
                  fetchTaskDetailsFromDB();
                  // Select master nodes and apply isMaster flags immediately.
                  selectAndApplyMasters();
                  // Set all the in-memory node names.
                  setNodeNames(u);

                  // Set non on-prem node UUIDs.
                  setCloudNodeUuids(u);
                  // Update on-prem node UUIDs.
                  updateOnPremNodeUuidsOnTaskParams();
                  // Set the prepared data to universe in-memory.
                  updateUniverseNodesAndSettings(u, taskParams(), false);
                  for (Cluster cluster : taskParams().clusters) {
                    u.getUniverseDetails()
                        .upsertCluster(cluster.userIntent, cluster.placementInfo, cluster.uuid);
                  }
                  // There is a rare possibility that this succeeds and
                  // saving the Universe fails. It is ok because the retry
                  // will just fail.
                  updateTaskDetailsInDB(taskParams());
                }
              });
      boolean cacheYCQLAuthPass =
          primaryCluster.userIntent.enableYCQL
              && primaryCluster.userIntent.enableYCQLAuth
              && !primaryCluster.userIntent.defaultYcqlPassword;
      boolean cacheYSQLAuthPass =
          primaryCluster.userIntent.enableYSQL
              && primaryCluster.userIntent.enableYSQLAuth
              && !primaryCluster.userIntent.defaultYsqlPassword;
      if (cacheYCQLAuthPass || cacheYSQLAuthPass) {
        if (isFirstTry()) {
          log.debug("Storing passwords in memory");
          passwordStore.put(
              universe.getUniverseUUID(), new AuthPasswords(ycqlPassword, ysqlPassword));
        } else {
          log.debug("Reading password for {}", universe.getUniverseUUID());
          // Read from the in-memory store on retry.
          AuthPasswords passwords = passwordStore.getIfPresent(universe.getUniverseUUID());
          if (passwords == null) {
            throw new RuntimeException(
                "Auth passwords are not found. Platform might have restarted"
                    + " or task might have expired");
          }
          ycqlPassword = passwords.ycqlPassword;
          ysqlPassword = passwords.ysqlPassword;
        }
      }

      createInstanceExistsCheckTasks(universe.getUniverseUUID(), universe.getNodes());

      // Create preflight node check tasks for on-prem nodes.
      createPreflightNodeCheckTasks(universe, taskParams().clusters);

      // Provision the nodes.
      // State checking is enabled because the subtasks are not idempotent.
      createProvisionNodeTasks(
          universe,
          taskParams().nodeDetailsSet,
          false /* isShell */,
          false /* ignore node status check */,
          false /* ignoreUseCustomImageConfig */);

      Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);

      // Make sure clock skew is low enough.
      createWaitForClockSyncTasks(universe, taskParams().nodeDetailsSet)
          .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

      // Get the new masters from the node list.
      Set<NodeDetails> newMasters = PlacementInfoUtil.getMastersToProvision(primaryNodes);

      // Get the new tservers from the node list.
      Set<NodeDetails> newTservers =
          PlacementInfoUtil.getTserversToProvision(taskParams().nodeDetailsSet);

      // Start masters.
      createStartMasterProcessTasks(newMasters);

      // Start tservers on tserver nodes.
      createStartTserverProcessTasks(newTservers, isYSQLEnabled);

      // Set the node state to live.
      createSetNodeStateTasks(taskParams().nodeDetailsSet, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Start ybc process on all the nodes
      if (taskParams().isEnableYbc()) {
        createStartYbcProcessTasks(
            taskParams().nodeDetailsSet, taskParams().getPrimaryCluster().userIntent.useSystemd);
        createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      createConfigureUniverseTasks(primaryCluster);

      // Create Load Balancer map to add nodes to load balancer
      Map<LoadBalancerPlacement, LoadBalancerConfig> loadBalancerMap =
          createLoadBalancerMap(taskParams(), null, null, null);
      createManageLoadBalancerTasks(loadBalancerMap);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      log.debug("Unlocking universe {}", getUniverse().getUniverseUUID());
      Universe universe = unlockUniverseForUpdate();
      if (universe != null && universe.getUniverseDetails().updateSucceeded) {
        log.debug("Removing passwords for {}", universe.getUniverseUUID());
        passwordStore.invalidate(universe.getUniverseUUID());
        if (universe.getConfig().getOrDefault(Universe.USE_CUSTOM_IMAGE, "false").equals("true")
            && taskParams().overridePrebuiltAmiDBVersion) {
          universe.updateConfig(
              ImmutableMap.of(Universe.USE_CUSTOM_IMAGE, Boolean.toString(false)));
          universe.save();
        }
      }
    }
    log.info("Finished {} task.", getName());
  }
}
