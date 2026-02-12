// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class ReplaceNodeInUniverse extends EditUniverseTaskBase {

  @Inject
  protected ReplaceNodeInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    addBasicPrecheckTasks();
    if (isFirstTry()) {
      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg =
            "No node " + taskParams().nodeName + " found in universe " + universe.getName();
        log.error(msg);
        throw new RuntimeException(msg);
      }

      // Generate new nodeDetails from existing node.
      NodeDetails newNode = PlacementInfoUtil.createToBeAddedNode(currentNode);
      // Set the replacement node to toBeRemoved state.
      setToBeRemovedState(currentNode);

      // Add the new nodeDetails to the universe.
      taskParams().nodeDetailsSet.add(newNode);

      configureTaskParams(universe);
    }
  }

  @Override
  protected void freezeUniverseInTxn(Universe universe) {
    super.freezeUniverseInTxn(universe);
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    taskParams().azUuid = currentNode.azUuid;
    taskParams().placementUuid = currentNode.placementUuid;
  }

  @Override
  public void run() {
    if (maybeRunOnlyPrechecks()) {
      return;
    }
    log.info("Started {} task for uuid={}", getName(), taskParams().getUniverseUUID());
    checkUniverseVersion();
    String errorString = null;
    Universe universe = null;

    try {
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
      // On retry, the current node may already be removed from the universe
      //   but task may have failed after destroy subtask, hence need to find by taskParams.
      Cluster taskParamsCluster = universe.getCluster(taskParams().placementUuid);
      log.debug(
          "Replacing node: {} in cluster: {} for universe: {}",
          taskParams().nodeName,
          taskParamsCluster.uuid,
          universe.getUniverseUUID());
      Set<NodeDetails> addedMasters = getAddedMasters();
      Set<NodeDetails> removedMasters = getRemovedMasters();

      // Update the cluster in memory.
      universe
          .getUniverseDetails()
          .upsertCluster(
              taskParamsCluster.userIntent,
              taskParamsCluster.getPartitions(),
              taskParamsCluster.placementInfo,
              taskParamsCluster.uuid);

      // Replace with new node.
      editCluster(
          universe,
          taskParams().clusters,
          taskParamsCluster,
          getNodesInCluster(taskParamsCluster.uuid, addedMasters),
          getNodesInCluster(taskParamsCluster.uuid, removedMasters),
          true /* force */);

      createUpdateUniverseIntentTask(taskParamsCluster);

      // Wait for the master leader to hear from all tservers.
      // NOTE: Universe expansion will fail in the master leader failover scenario - if a node
      // is down externally for >15 minutes and the master leader then marks the node down for
      // real. Then that down TServer will timeout this task and universe expansion will fail.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      errorString = t.getMessage();
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      releaseReservedNodes();
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate(taskParams().getUniverseUUID(), errorString);
      log.info("Finished {} task.", getName());
    }
  }
}
