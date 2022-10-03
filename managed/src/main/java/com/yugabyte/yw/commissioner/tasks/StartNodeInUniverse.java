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
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collection;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang.StringUtils;

/**
 * Class contains the tasks to start a node in a given universe. It starts the tserver process and
 * the master process if needed.
 */
@Slf4j
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
          taskParams().universeUUID,
          universe.name);

      currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " found in universe " + universe.name;
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

      // Update node state to Starting
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
      boolean startTserver = true;
      if (cluster.userIntent.dedicatedNodes) {
        startTserver = currentNode.dedicatedTo == ServerType.TSERVER;
        startMaster = !startTserver;
      }
      final Collection<NodeDetails> nodeCollection = ImmutableList.of(currentNode);
      if (startMaster) {
        // Clean the master addresses in the conf file for the current node so that
        // the master comes up as a shell master.
        createConfigureServerTasks(
                nodeCollection,
                params -> {
                  params.isMasterInShellMode = true;
                  params.updateMasterAddrsOnly = true;
                  params.isMaster = true;
                })
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        // Set gflags for master.
        createGFlagsOverrideTasks(
            nodeCollection,
            ServerType.MASTER,
            true /*isShell */,
            VmUpgradeTaskType.None,
            false /*ignoreUseCustomImageConfig*/);

        // Start a master process.
        createStartMasterTasks(nodeCollection)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Mark node as isMaster in YW DB.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, true /* isAdd */)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for the master to be responsive.
        createWaitForServersTasks(nodeCollection, ServerType.MASTER)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        // Add stopped master to the quorum.
        createChangeConfigTask(currentNode, true /* isAdd */, SubTaskGroupType.ConfigureUniverse);
      }
      if (startTserver) {
        // Start the tserver process
        createTServerTaskForNode(currentNode, "start")
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Mark the node process flags as true.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, true /* isAdd */)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for the tablet server to be responsive.
        createWaitForServersTasks(nodeCollection, ServerType.TSERVER)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      }

      // Start yb-controller process
      if (universe.isYbcEnabled()) {
        createStartYbcTasks(nodeCollection)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for yb-controller to be responsive on each node.
        createWaitForYbcServerTask(nodeCollection)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      if (startMaster) {
        // Update all server conf files with new master information.
        createMasterInfoUpdateTask(universe, currentNode);

        // Update the master addresses on the target universes whose source universe belongs to
        // this task.
        createXClusterConfigUpdateMasterAddressesTask();
      }

      // Update node state to running
      createSetNodeStateTask(currentNode, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingNode);

      // Update the DNS entry for this universe.
      UniverseDefinitionTaskParams.UserIntent userIntent =
          universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid).userIntent;
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, userIntent)
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
      // Reset the state, on any failure, so that the actions can be retried.
      if (currentNode != null) {
        setNodeState(taskParams().nodeName, currentNode.state);
      }
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
