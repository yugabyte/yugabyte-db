// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.CustomerTask.TaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.concurrent.atomic.AtomicBoolean;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
public class MasterFailover extends UniverseDefinitionTaskBase {

  private String replacementMasterName;

  @Inject
  protected MasterFailover(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  private NodeDetails runBasicChecks(Universe universe) {
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (node == null) {
      String errMsg =
          String.format(
              "Node %s is not found in universe %s",
              taskParams().nodeName, universe.getUniverseUUID());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    if (!node.isMaster && node.masterState != MasterState.ToStop) {
      // On first try, isMaster must be set. On retry, masterState must be set to ToStop.
      String errMsg =
          String.format(
              "Node %s must be a master in the universe %s",
              taskParams().nodeName, universe.getUniverseUUID());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    return node;
  }

  private void freezeUniverseInTxn(Universe universe) {
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (node == null) {
      String errMsg =
          String.format(
              "Node %s is not found in universe %s",
              taskParams().nodeName, universe.getUniverseUUID());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    if (!node.isMaster) {
      String errMsg =
          String.format(
              "Node %s must be a master in the universe %s",
              taskParams().nodeName, universe.getUniverseUUID());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    NodeDetails replacementMaster = universe.getNode(replacementMasterName);
    if (replacementMaster.masterState == null) {
      replacementMaster.masterState = MasterState.ToStart;
    }
    node.masterState = MasterState.ToStop;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    Universe universe = getUniverse();
    if (isFirstTry) {
      boolean autoSyncMasterAddrs =
          universe.getNodes().stream().anyMatch(n -> n.autoSyncMasterAddrs);
      if (autoSyncMasterAddrs) {
        String errMsg =
            String.format("Task %s task must be run before failover", TaskType.SyncMasterAddresses);
        log.error(errMsg);
        throw new RuntimeException(errMsg);
      }
    }
    runBasicChecks(universe);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    NodeDetails node = runBasicChecks(universe);
    super.addBasicPrecheckTasks();
    // Pick new only on first try.
    replacementMasterName = super.findReplacementMaster(universe, node, isFirstTry());
    if (replacementMasterName == null) {
      String errMsg =
          String.format(
              "No replacement is found for master %s in the universe %s",
              taskParams().nodeName, universe.getUniverseUUID());
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
  }

  @Override
  public void run() {
    log.info(
        "MasterFailoverTask with name {} from universe uuid={}",
        taskParams().nodeName,
        taskParams().getUniverseUUID());
    checkUniverseVersion();
    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
    try {
      preTaskActions();
      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      AtomicBoolean updateMasterAddrsOnStoppedNode = new AtomicBoolean();
      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;
      if (isTserverAliveOnNode(currentNode, universe.getMasterAddresses())) {
        // Auto master update is enabled and tserver is not reachable. This means addresses update
        // can be skipped for this node as auto-update will take care later.
        updateMasterAddrsOnStoppedNode.set(true);
      }
      createUpdateUniverseFieldsTask(
          u -> {
            NodeDetails unreachableNode = u.getNode(taskParams().nodeName);
            // Always set it to true just in case master comes up before the sync so that it can be
            // stopped.
            unreachableNode.autoSyncMasterAddrs = true;
          });
      log.debug(
          "Update master addresses on stopped master node is {}", updateMasterAddrsOnStoppedNode);
      // Even though we are calling the createMasterReplacementTasks method, we have ensured that a
      // new Master node replacement is available, and so the code will always follow the else-if
      // branch within this function, which is equivalent to calling the
      // createStartMasterOnNodeTasks function with a couple of node state change tasks.
      createMasterReplacementTasks(
          universe,
          currentNode,
          () -> replacementMasterName == null ? null : universe.getNode(replacementMasterName),
          super.instanceExists(taskParams()),
          true /*ignoreStopErrors*/,
          true /*ignoreMasterAddrsUpdateError*/,
          true /*keepTserverRunning*/);
      createSetNodeStateTask(currentNode, NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      createSwamperTargetUpdateTask(false);
      createMarkUniverseUpdateSuccessTasks();
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
