/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.LoadBalancerConfig;
import com.yugabyte.yw.models.helpers.LoadBalancerPlacement;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
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

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      // Verify the task params.
      verifyParams(UniverseOpType.CREATE);
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      Customer customer = Customer.get(universe.getCustomerId());
      if (!confGetter.getConfForScope(customer, CustomerConfKeys.useAnsibleProvisioning)) {
        for (Cluster cluster : taskParams().clusters) {
          // Local provider can still use cron.
          if (!cluster.userIntent.useSystemd
              && cluster.userIntent.providerType != CloudType.local) {
            log.warn(
                "cron based universe cannot be created with YNP, will fallback to ansible "
                    + "provisioning");
            break;
          }
        }
      }
    }
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    if (isFirstTry()) {
      configureTaskParams(universe);
      // Validate preview flags
      universe
          .getUniverseDetails()
          .clusters
          .forEach(
              cluster -> {
                try {
                  String errMsg =
                      GFlagsUtil.checkPreviewGFlagsOnSpecificGFlags(
                          cluster.userIntent.specificGFlags,
                          gFlagsValidation,
                          cluster.userIntent.ybSoftwareVersion);
                  if (errMsg != null) {
                    throw new PlatformServiceException(BAD_REQUEST, errMsg);
                  }
                } catch (IOException e) {
                  log.error(
                      "Error while checking preview flags on the cluster: {}", cluster.uuid, e);
                  throw new PlatformServiceException(
                      INTERNAL_SERVER_ERROR,
                      "Error while checking preview flags on cluster: " + cluster.uuid);
                }
              });
    }
  }

  // This is invoked only on first try.
  protected void configureTaskParams(Universe universe) {
    // Select master nodes and apply isMaster flags immediately.
    selectAndApplyMasters();
    // Set all the in-memory node names.
    setNodeNames(universe);
    // Set non on-prem node UUIDs.
    setCloudNodeUuids(universe);
    // Update on-prem node UUIDs in task params but do not commit yet.
    updateOnPremNodeUuidsOnTaskParams(false /* commit changes */);
    // Set the communication ports to the node in the task params in memory to run prechecks.
    setCommunicationPortsForNodes(true);
    // Set the prepared data to universe in-memory required to run the prechecks.
    updateUniverseNodesAndSettings(universe, taskParams(), false);
    // Upsert the clusters to universe in-memory required to run the prechecks.
    for (Cluster cluster : taskParams().clusters) {
      universe
          .getUniverseDetails()
          .upsertCluster(
              cluster.userIntent, cluster.getPartitions(), cluster.placementInfo, cluster.uuid);
    }
    // Create preflight node check tasks for on-prem nodes.
    createPreflightNodeCheckTasks(universe, taskParams().clusters);
    // Create certificate config check tasks for on-prem nodes.
    createCheckCertificateConfigTask(universe, taskParams().clusters);
  }

  private void freezeUniverseInTxn(Universe universe) {
    // Confirm the nodes on hold.
    commitReservedNodes();
    // Set the communication ports to the node for persisting in DB.
    setCommunicationPortsForNodes(true);
    // Set the prepared data to universe for persisting in DB.
    updateUniverseNodesAndSettings(universe, taskParams(), false);
    // Upsert the clusters to universe for persisting in DB.
    for (Cluster cluster : taskParams().clusters) {
      universe
          .getUniverseDetails()
          .upsertCluster(
              cluster.userIntent, cluster.getPartitions(), cluster.placementInfo, cluster.uuid);
    }
    // Update task params.
    updateTaskDetailsInDB(taskParams());
  }

  // Store the passwords in the temporary variables first and cache.
  // DB does not store the actual passwords.
  private void cachePasswordsIfNeeded() {
    if (isFirstTry()) {
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      boolean cacheYCQLAuthPass =
          primaryCluster.userIntent.enableYCQL
              && primaryCluster.userIntent.enableYCQLAuth
              && !primaryCluster.userIntent.defaultYcqlPassword;
      boolean cacheYSQLAuthPass =
          primaryCluster.userIntent.enableYSQL
              && primaryCluster.userIntent.enableYSQLAuth
              && !primaryCluster.userIntent.defaultYsqlPassword;
      if (cacheYCQLAuthPass || cacheYSQLAuthPass) {
        ycqlPassword = primaryCluster.userIntent.ycqlPassword;
        ysqlPassword = primaryCluster.userIntent.ysqlPassword;
        log.debug("Storing passwords in memory");
        passwordStore.put(
            taskParams().getUniverseUUID(), new AuthPasswords(ycqlPassword, ysqlPassword));
      }
    }
  }

  private void retrievePasswordsIfNeeded() {
    if (!isFirstTry()) {
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      boolean isYCQLAuthPassCached =
          primaryCluster.userIntent.enableYCQL
              && primaryCluster.userIntent.enableYCQLAuth
              && !primaryCluster.userIntent.defaultYcqlPassword;
      boolean isYSQLAuthPassCached =
          primaryCluster.userIntent.enableYSQL
              && primaryCluster.userIntent.enableYSQLAuth
              && !primaryCluster.userIntent.defaultYsqlPassword;
      if (isYCQLAuthPassCached || isYSQLAuthPassCached) {
        log.debug("Reading password for {}", taskParams().getUniverseUUID());
        // Read from the in-memory store on retry.
        AuthPasswords passwords = passwordStore.getIfPresent(taskParams().getUniverseUUID());
        if (passwords == null) {
          throw new RuntimeException(
              "Auth passwords are not found. Platform might have restarted"
                  + " or task might have expired");
        }
        ycqlPassword = passwords.ycqlPassword;
        ysqlPassword = passwords.ysqlPassword;
      }
    }
  }

  // CreateUniverse can be retried, so all tasks within should be idempotent. For an example of how
  // to achieve idempotence or to make retries more performant, see the createProvisionNodeTasks
  // pattern below
  @Override
  public void run() {
    log.info("Started {} task.", getName());
    // Cache the password before it is redacted.
    cachePasswordsIfNeeded();
    Universe universe = null;
    try {
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
      Cluster primaryCluster = taskParams().getPrimaryCluster();

      boolean isYSQLEnabled = primaryCluster.userIntent.enableYSQL;

      retrievePasswordsIfNeeded();

      createPersistUseClockboundTask();

      createInstanceExistsCheckTasks(universe.getUniverseUUID(), taskParams(), universe.getNodes());

      boolean deleteCapacityReservation =
          createCapacityReservationsIfNeeded(
              taskParams().nodeDetailsSet,
              CapacityReservationUtil.OperationType.CREATE,
              node ->
                  node.state == NodeDetails.NodeState.ToBeAdded
                      || node.state == NodeDetails.NodeState.Adding);
      // Provision the nodes.
      // State checking is enabled because the subtasks are not idempotent.
      createProvisionNodeTasks(
          universe,
          taskParams().nodeDetailsSet,
          false /* ignore node status check */,
          true /* do validation of gflags */,
          setupServerParams -> {
            setupServerParams.rebootNodeAllowed = true;
          },
          null /* install software param customizer */,
          gFlagsParams -> {
            gFlagsParams.resetMasterState = true;
            gFlagsParams.masterJoinExistingCluster = false;
          });
      if (deleteCapacityReservation) {
        createDeleteCapacityReservationTask();
      }

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
      createStartTserverProcessTasks(newTservers, isYSQLEnabled, true /* skipYSQLServerCheck */);

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

      createConfigureUniverseTasks(
          primaryCluster, newMasters, newTservers, null /* gflagsUpgradeSubtasks */);

      if (primaryCluster.isGeoPartitioned() && primaryCluster.userIntent.enableYSQL) {
        createTablespacesTasks(primaryCluster.getPartitions(), false);
      }

      // Create Load Balancer map to add nodes to load balancer
      Map<LoadBalancerPlacement, LoadBalancerConfig> loadBalancerMap =
          createLoadBalancerMap(taskParams(), null, null, null);
      createManageLoadBalancerTasks(loadBalancerMap);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      if (universe != null) {
        clearCapacityReservationOnError(t, Universe.getOrBadRequest(universe.getUniverseUUID()));
      }
      throw t;
    } finally {
      releaseReservedNodes();
      if (universe != null) {
        // Mark the update of the universe as done. This will allow future edits/updates to the
        // universe to happen.
        log.debug("Unlocking universe {}", universe.getUniverseUUID());
        universe = unlockUniverseForUpdate();
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
    }
    log.info("Finished {} task.", getName());
  }
}
