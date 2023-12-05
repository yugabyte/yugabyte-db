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

import static com.yugabyte.yw.common.Util.areMastersUnderReplicated;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Class contains the tasks to start a node in a given universe. It starts the tserver process and
 * the master process if needed.
 */
@Slf4j
@Retryable
public class StartNodeInUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected StartNodeInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    NodeDetails currentNode = null;
    try {
      checkUniverseVersion();
      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      log.info(
          "Start Node with name {} from universe {} ({})",
          taskParams().nodeName,
          taskParams().getUniverseUUID(),
          universe.getName());

      boolean followerLagCheckEnabled =
          confGetter.getConfForScope(universe, UniverseConfKeys.followerLagCheckEnabled);

      currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg =
            "No node " + taskParams().nodeName + " found in universe " + universe.getName();
        log.error(msg);
        throw new RuntimeException(msg);
      }
      UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(currentNode.placementUuid);

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;
      if (!instanceExists(taskParams())) {
        String msg = "No instance exists for " + taskParams().nodeName;
        log.error(msg);
        throw new RuntimeException(msg);
      }

      preTaskActions();

      // Update node state to Starting.
      createSetNodeStateTask(currentNode, NodeState.Starting)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Bring up any masters, as needed:
      // - Masters should be under replicated;
      // - If GP is on, currentNode should be in default region (when GP and default
      // region is defined, we can have masters only in the default region).
      String defaultRegionCode = PlacementInfoUtil.getDefaultRegionCode(taskParams());
      boolean startMaster =
          areMastersUnderReplicated(currentNode, universe)
              && (defaultRegionCode == null
                  || StringUtils.equals(defaultRegionCode, currentNode.cloudInfo.region));

      if (!startMaster
          && (currentNode.masterState == MasterState.ToStart
              || currentNode.masterState == MasterState.Configured)) {
        // Make sure that the non under-replicated case is caused by this master.
        startMaster = currentNode.isMaster;
      }

      boolean startTserver = true;
      if (cluster.userIntent.dedicatedNodes) {
        startTserver = currentNode.dedicatedTo == ServerType.TSERVER;
        startMaster = !startTserver;
      }

      if (startMaster) {
        if (currentNode.masterState == null) {
          saveNodeStatus(
              taskParams().nodeName, NodeStatus.builder().masterState(MasterState.ToStart).build());
        }
        createStartMasterOnNodeTasks(universe, currentNode, null);
      }
      final Set<NodeDetails> nodeCollection = ImmutableSet.of(currentNode);
      if (startTserver) {
        // Update master addresses for tservers.
        createConfigureServerTasks(nodeCollection, params -> params.updateMasterAddrsOnly = true)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
        // Start tservers on tserver nodes.
        createStartTserverProcessTasks(nodeCollection, cluster.userIntent.isYSQLAuthEnabled());

        if (followerLagCheckEnabled) {
          createCheckFollowerLagTask(currentNode, ServerType.TSERVER)
              .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
        }
      }

      // Start yb-controller process
      if (universe.isYbcEnabled()) {
        createStartYbcTasks(nodeCollection)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for yb-controller to be responsive on each node.
        createWaitForYbcServerTask(nodeCollection)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Update node state to running
      createSetNodeStateTask(currentNode, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // Add node to load balancer.
      createManageLoadBalancerTasks(
          createLoadBalancerMap(
              universe.getUniverseDetails(),
              ImmutableList.of(cluster),
              null,
              ImmutableSet.of(currentNode)));

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, universe)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // Update the swamper target file.
      // It is required because the node could be removed from the swamper file
      // between the Stop/Start actions as Inactive.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Mark universe update success to true
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.StartingNode);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
