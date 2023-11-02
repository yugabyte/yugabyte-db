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
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Allows the removal of a node from a universe. Ensures the task waits for the right set of
// server data move primitives. And stops using the underlying instance, though YW still owns it.
@Slf4j
@Retryable
public class RemoveNodeFromUniverse extends UniverseTaskBase {

  @Inject
  protected RemoveNodeFromUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    log.info(
        "Started {} task for node {} in univ uuid={}",
        getName(),
        taskParams().nodeName,
        taskParams().getUniverseUUID());
    NodeDetails currentNode = null;
    try {
      checkUniverseVersion();

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg =
            "No node " + taskParams().nodeName + " found in universe " + universe.getName();
        log.error(msg);
        throw new RuntimeException(msg);
      }

      if (isFirstTry()) {
        currentNode.validateActionOnState(NodeActionType.REMOVE);
      }

      preTaskActions();

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;

      String masterAddrs = universe.getMasterAddresses();

      Cluster currCluster =
          universe.getUniverseDetails().getClusterByUuid(taskParams().placementUuid);
      UserIntent userIntent = currCluster.userIntent;
      PlacementInfo pi = currCluster.placementInfo;

      // Update Node State to being removed.
      createSetNodeStateTask(currentNode, NodeState.Removing)
          .setSubTaskGroupType(SubTaskGroupType.RemovingNode);

      boolean instanceAlive = instanceExists(taskParams());

      if (instanceAlive) {
        // Remove the master on this node from master quorum and update its state from YW DB,
        // only if it reachable.
        boolean masterReachable = isMasterAliveOnNode(currentNode, masterAddrs);
        log.info("Master {}, reachable = {}.", currentNode.cloudInfo.private_ip, masterReachable);
        if (currentNode.isMaster) {
          // Wait for Master Leader before doing any MasterChangeConfig operations.
          createWaitForMasterLeaderTask()
              .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
          if (masterReachable) {
            createChangeConfigTask(currentNode, false, SubTaskGroupType.WaitForDataMigration);
            createStopMasterTasks(new HashSet<>(Arrays.asList(currentNode)))
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
            createWaitForMasterLeaderTask()
                .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
          } else {
            createChangeConfigTask(currentNode, false, SubTaskGroupType.WaitForDataMigration);
          }
        }

      } else {
        if (currentNode.isMaster) {
          createWaitForMasterLeaderTask()
              .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
          createChangeConfigTask(currentNode, false, SubTaskGroupType.WaitForDataMigration);
        }
      }

      // Mark the tserver as blacklisted on the master leader.
      createPlacementInfoTask(new HashSet<>(Arrays.asList(currentNode)))
          .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

      // Wait for tablet quorums to remove the blacklisted tserver. Do not perform load balance
      // if node is not reachable so as to avoid cases like 1 node in an 3 node cluster is being
      // removed and we know LoadBalancer will not be able to handle that.
      if (instanceAlive) {
        Collection<NodeDetails> nodesExcludingCurrentNode = new HashSet<>(universe.getNodes());
        nodesExcludingCurrentNode.remove(currentNode);
        int rfInZone =
            PlacementInfoUtil.getZoneRF(
                pi,
                currentNode.cloudInfo.cloud,
                currentNode.cloudInfo.region,
                currentNode.cloudInfo.az);
        long nodesActiveInAZExcludingCurrentNode =
            PlacementInfoUtil.getNumActiveTserversInZone(
                nodesExcludingCurrentNode,
                currentNode.cloudInfo.cloud,
                currentNode.cloudInfo.region,
                currentNode.cloudInfo.az);

        if (rfInZone == -1) {
          log.error(
              "Unexpected placement info in universe {} {} {}",
              universe.getName(),
              rfInZone,
              nodesActiveInAZExcludingCurrentNode);
          throw new RuntimeException(
              "Error getting placement info for cluster with node: " + currentNode.nodeName);
        }

        // Since numNodes can never be less, that will mean there is a potential node to move
        // data to.
        if (userIntent.numNodes > userIntent.replicationFactor) {
          // We only want to move data if the number of nodes in the zone are more than or equal
          //  the RF of the zone.
          // We would like to remove currentNode whether it is in live/stopped state
          if (nodesActiveInAZExcludingCurrentNode >= rfInZone) {
            createWaitForDataMoveTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
          }
        }

        // Remove node from load balancer.
        createManageLoadBalancerTasks(
            createLoadBalancerMap(
                universe.getUniverseDetails(),
                Arrays.asList(currCluster),
                new HashSet<>(Arrays.asList(currentNode)),
                null));
        createTServerTaskForNode(currentNode, "stop", true /*isIgnoreErrors*/)
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

        if (universe.isYbcEnabled()) {
          createStopYbControllerTasks(
                  new HashSet<>(Arrays.asList(currentNode)), true /*isIgnoreErrors*/)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }
      }

      // Remove master status (even when it does not exists or is not reachable).
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, false)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      // Update the master addresses on the target universes whose source universe belongs to
      // this task.
      createXClusterConfigUpdateMasterAddressesTask();

      // Remove its tserver status in DB.
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, false)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, universe)
          .setSubTaskGroupType(SubTaskGroupType.RemovingNode);

      // Update Node State to Removed
      createSetNodeStateTask(currentNode, NodeState.Removed)
          .setSubTaskGroupType(SubTaskGroupType.RemovingNode);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Mark universe task state to success
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.RemovingNode);

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
}
