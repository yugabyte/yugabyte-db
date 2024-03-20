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
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.NodeActionFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collections;
import java.util.EnumSet;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
public class StopNodeInUniverse extends UniverseDefinitionTaskBase {

  @Inject private RuntimeConfGetter confGetter;

  @Inject
  protected StopNodeInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  protected NodeDetails findNewMasterIfApplicable(Universe universe, NodeDetails currentNode) {
    boolean startMasterOnStopNode = confGetter.getGlobalConf(GlobalConfKeys.startMasterOnStopNode);
    if (startMasterOnStopNode && NodeActionFormData.startMasterOnStopNode) {
      return super.findReplacementMaster(universe, currentNode);
    }
    return null;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    Universe universe = getUniverse();
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    if (currentNode == null) {
      String msg = "No node " + taskParams().nodeName + " found in universe " + universe.getName();
      log.error(msg);
      throw new RuntimeException(msg);
    }
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    if (currentNode.isTserver) {
      createNodePrecheckTasks(
          currentNode,
          EnumSet.of(ServerType.TSERVER),
          SubTaskGroupType.StoppingNodeProcesses,
          null);
    }
    if (isFirstTry()) {
      verifyClustersConsistency();
    }
  }

  private void freezeUniverseInTxn(Universe universe) {
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (node == null) {
      String msg = "No node " + taskParams().nodeName + " found in universe " + universe.getName();
      log.error(msg);
      throw new RuntimeException(msg);
    }
    if (node.isMaster) {
      NodeDetails newMasterNode = findNewMasterIfApplicable(universe, node);
      if (newMasterNode != null && newMasterNode.masterState == null) {
        newMasterNode.masterState = MasterState.ToStart;
      }
      node.masterState = MasterState.ToStop;
    }
  }

  @Override
  public void run() {
    log.info(
        "Stop Node with name {} from universe uuid={}",
        taskParams().nodeName,
        taskParams().getUniverseUUID());
    checkUniverseVersion();
    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
    try {
      NodeDetails currentNode = universe.getNode(taskParams().nodeName);

      preTaskActions();
      List<NodeDetails> nodeList = Collections.singletonList(currentNode);

      if (currentNode.isTserver) {
        clearLeaderBlacklistIfAvailable(SubTaskGroupType.StoppingNodeProcesses);
      }

      // Update Node State to Stopping
      createSetNodeStateTask(currentNode, NodeState.Stopping)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;
      boolean instanceExists = instanceExists(taskParams());
      if (instanceExists) {
        if (currentNode.isTserver) {
          stopProcessesOnNode(
              currentNode,
              EnumSet.of(ServerType.TSERVER),
              true,
              false,
              SubTaskGroupType.StoppingNodeProcesses);
          // Remove leader blacklist.
          removeFromLeaderBlackListIfAvailable(nodeList, SubTaskGroupType.StoppingNodeProcesses);
        }

        // Stop Yb-controller on this node.
        if (universe.isYbcEnabled()) {
          createStopYbControllerTasks(nodeList)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }
      }
      if (currentNode.isTserver) {
        // Update the per process state in YW DB.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, false)
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      createMasterReplacementTasks(
          universe,
          currentNode,
          () -> findNewMasterIfApplicable(universe, currentNode),
          instanceExists);

      // Update Node State to Stopped
      createSetNodeStateTask(currentNode, NodeState.Stopped)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, universe)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      // Mark universe task state to success
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.StoppingNode);

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
