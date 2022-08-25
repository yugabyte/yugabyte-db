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
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class StopNodeInUniverse extends UniverseTaskBase {

  protected boolean isBlacklistLeaders;
  protected int leaderBacklistWaitTimeMs;

  @Inject
  protected StopNodeInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    NodeDetails currentNode = null;
    boolean hitException = false;

    try {
      checkUniverseVersion();

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      log.info(
          "Stop Node with name {} from universe {} ({})",
          taskParams().nodeName,
          taskParams().universeUUID,
          universe.name);

      isBlacklistLeaders =
          runtimeConfigFactory.forUniverse(universe).getBoolean(Util.BLACKLIST_LEADERS);
      leaderBacklistWaitTimeMs =
          runtimeConfigFactory.forUniverse(universe).getInt(Util.BLACKLIST_LEADER_WAIT_TIME_MS);

      currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " found in universe " + universe.name;
        log.error(msg);
        throw new RuntimeException(msg);
      }

      preTaskActions();
      isBlacklistLeaders = isBlacklistLeaders && isLeaderBlacklistValidRF(currentNode.nodeName);
      if (isBlacklistLeaders) {
        List<NodeDetails> tServerNodes = universe.getTServers();
        createModifyBlackListTask(tServerNodes, false /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      // Update Node State to Stopping
      createSetNodeStateTask(currentNode, NodeState.Stopping)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;
      if (instanceExists(taskParams())) {

        // set leader blacklist and poll
        if (isBlacklistLeaders) {
          createModifyBlackListTask(
                  Arrays.asList(currentNode), true /* isAdd */, true /* isLeaderBlacklist */)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          createWaitForLeaderBlacklistCompletionTask(leaderBacklistWaitTimeMs)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }

        // Stop the tserver.
        createTServerTaskForNode(currentNode, "stop")
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

        // remove leader blacklist
        if (isBlacklistLeaders) {
          createModifyBlackListTask(
                  Arrays.asList(currentNode), false /* isAdd */, true /* isLeaderBlacklist */)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }

        // Stop Yb-controller on this node.
        if (universe.isYbcEnabled()) {
          createStopYbControllerTasks(Arrays.asList(currentNode))
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }

        // Stop the master process on this node.
        if (currentNode.isMaster) {
          createStopMasterTasks(new HashSet<NodeDetails>(Arrays.asList(currentNode)))
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          createWaitForMasterLeaderTask()
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }
      }

      // Update the per process state in YW DB.
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, false)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      if (currentNode.isMaster) {
        createChangeConfigTask(
            currentNode,
            false /* isAdd */,
            SubTaskGroupType.ConfigureUniverse,
            true /* useHostPort */);
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, false)
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        // Update the master addresses on the target universes whose source universe belongs to
        // this task.
        createXClusterConfigUpdateMasterAddressesTask();
      }

      // Update Node State to Stopped
      createSetNodeStateTask(currentNode, NodeState.Stopped)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      // Update the DNS entry for this universe.
      UniverseDefinitionTaskParams.UserIntent userIntent =
          universe.getUniverseDetails().getClusterByUuid(currentNode.placementUuid).userIntent;
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, userIntent)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      // Mark universe task state to success
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      hitException = true;
      throw t;
    } finally {
      try {
        // Reset the state, on any failure, so that the actions can be retried.
        if (currentNode != null && hitException) {
          setNodeState(taskParams().nodeName, currentNode.state);
        }

        // remove leader blacklist for current node if task failed and leader blacklist is not
        // removed
        if (isBlacklistLeaders) {
          // Clear previous subtasks if any.
          getRunnableTask().reset();
          createModifyBlackListTask(
                  Arrays.asList(currentNode), false /* isAdd */, true /* isLeaderBlacklist */)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          getRunnableTask().runSubTasks();
        }
      } finally {
        unlockUniverseForUpdate();
      }
    }

    log.info("Finished {} task.", getName());
  }
}
