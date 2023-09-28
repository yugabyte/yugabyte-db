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
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.NodeActionFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Allows the removal of a node from a universe. Ensures the task waits for the right set of
// server data move primitives. And stops using the underlying instance, though YW still owns it.
@Slf4j
@Retryable
public class RemoveNodeFromUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected RemoveNodeFromUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  protected NodeDetails findNewMasterIfApplicable(Universe universe, NodeDetails currentNode) {
    boolean startMasterOnRemoveNode =
        confGetter.getGlobalConf(GlobalConfKeys.startMasterOnRemoveNode);
    if (startMasterOnRemoveNode && NodeActionFormData.startMasterOnRemoveNode) {
      return super.findReplacementMaster(universe, currentNode);
    }
    return null;
  }

  @Override
  public void run() {
    try {
      checkUniverseVersion();

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe =
          lockUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> {
                if (isFirstTry()) {
                  NodeDetails node = u.getNode(taskParams().nodeName);
                  if (node == null) {
                    String msg =
                        "No node " + taskParams().nodeName + " found in universe " + u.getName();
                    log.error(msg);
                    throw new RuntimeException(msg);
                  }
                  if (node.isMaster) {
                    NodeDetails newMasterNode = findNewMasterIfApplicable(u, node);
                    if (newMasterNode != null && newMasterNode.masterState == null) {
                      newMasterNode.masterState = MasterState.ToStart;
                    }
                    node.masterState = MasterState.ToStop;
                  }
                }
              });
      log.info(
          "Started {} task for node {} in univ uuid={}",
          getName(),
          taskParams().nodeName,
          taskParams().getUniverseUUID());
      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
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

      Cluster currCluster =
          universe.getUniverseDetails().getClusterByUuid(taskParams().placementUuid);
      UserIntent userIntent = currCluster.userIntent;
      PlacementInfo pi = currCluster.placementInfo;

      boolean instanceExists = instanceExists(taskParams());
      boolean masterReachable = false;
      // Update Node State to being removed.
      createSetNodeStateTask(currentNode, NodeState.Removing)
          .setSubTaskGroupType(SubTaskGroupType.RemovingNode);

      // Mark the tserver as blacklisted on the master leader.
      createPlacementInfoTask(Collections.singleton(currentNode))
          .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

      // Wait for tablet quorums to remove the blacklisted tserver. Do not perform load balance
      // if node is not reachable so as to avoid cases like 1 node in an 3 node cluster is being
      // removed and we know LoadBalancer will not be able to handle that.
      if (instanceExists) {
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
                Collections.singleton(currentNode),
                null));
        createTServerTaskForNode(currentNode, "stop", true /*isIgnoreErrors*/)
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

        if (universe.isYbcEnabled()) {
          createStopYbControllerTasks(Collections.singleton(currentNode), true /*isIgnoreErrors*/)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }
        masterReachable = isMasterAliveOnNode(currentNode, universe.getMasterAddresses());
      }

      // Remove its tserver status in DB.
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, false)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      // Create the master replacement tasks.
      createMasterReplacementTasks(
          universe,
          currentNode,
          () -> findNewMasterIfApplicable(universe, currentNode),
          masterReachable);

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
