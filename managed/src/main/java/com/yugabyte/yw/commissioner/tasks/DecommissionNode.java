/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@Abortable
public class DecommissionNode extends EditUniverseTaskBase {

  @Inject
  protected DecommissionNode(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  private void runBasicChecks(Universe universe) {
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    if (isFirstTry()) {
      currentNode.validateActionOnState(NodeActionType.DECOMMISSION);
    }
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    runBasicChecks(getUniverse());
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    if (isFirstTry()) {
      if (currentNode == null) {
        String msg =
            "No node " + taskParams().nodeName + " found in universe " + universe.getName();
        log.error(msg);
        throw new RuntimeException(msg);
      }
      setToBeRemovedState(currentNode);
      configureTaskParams(universe);
    }

    // Check again after locking.
    runBasicChecks(getUniverse());
    if (currentNode != null) {
      boolean alwaysWaitForDataMove =
          confGetter.getConfForScope(getUniverse(), UniverseConfKeys.alwaysWaitForDataMove);
      if (alwaysWaitForDataMove) {
        createCheckTabletsMovementAvailableTask(taskParams().nodeName);
      }
    }
    createComprehensivePrecheckTasks(universe);
    addBasicPrecheckTasks();
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
    log.info(
        "Started {} task for node {} in univ uuid={}",
        getName(),
        taskParams().nodeName,
        taskParams().getUniverseUUID());
    checkUniverseVersion();

    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
    try {
      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (isFirstTry()) {
        if (currentNode == null) {
          String msg =
              "No node " + taskParams().nodeName + " found in universe " + universe.getName();
          log.error(msg);
          throw new RuntimeException(msg);
        }
      }
      preTaskActions();
      Cluster taskParamsCluster =
          universe.getUniverseDetails().getClusterByUuid(taskParams().placementUuid);

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

      log.info("Decommission: added masters {}, removed masters {}", addedMasters, removedMasters);

      editCluster(
          universe,
          taskParams().clusters,
          taskParamsCluster,
          getNodesInCluster(taskParamsCluster.uuid, addedMasters),
          getNodesInCluster(taskParamsCluster.uuid, removedMasters),
          true /* force */);

      createUpdateUniverseIntentTask(taskParamsCluster, true /*updatePlacementInfo*/);

      // Mark universe task state to success
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.RemovingNode);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
