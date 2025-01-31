// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@ITask.Retryable
@ITask.Abortable
public class ReprovisionNode extends UniverseDefinitionTaskBase {

  @Inject
  protected ReprovisionNode(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  private void runBasicChecks(Universe universe) {
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);
    if (currentNode == null) {
      String msg = "No node " + taskParams().nodeName + " in universe " + universe.getName();
      log.error(msg);
      throw new RuntimeException(msg);
    }
    if (currentNode.isMaster || currentNode.isTserver) {
      String msg = "Cannot reprovision " + taskParams().nodeName + " while it is not stopped";
      log.error(msg);
      throw new RuntimeException(msg);
    }
    taskParams().azUuid = currentNode.azUuid;
    taskParams().placementUuid = currentNode.placementUuid;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    runBasicChecks(getUniverse());
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    // Check again after locking.
    runBasicChecks(getUniverse());
    if (!instanceExists(taskParams())) {
      String msg = "No instance exists for " + taskParams().nodeName;
      log.error(msg);
      throw new RuntimeException(msg);
    }
  }

  @Override
  public void run() {
    log.info(
        "Started {} task for node {} in univ uuid={}",
        getName(),
        taskParams().nodeName,
        taskParams().getUniverseUUID());
    try {
      checkUniverseVersion();

      // Update the DB to prevent other changes from happening.
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);

      NodeDetails currentNode = universe.getNode(taskParams().nodeName);

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;

      preTaskActions();

      // Update node state to Reprovisioning.
      createSetNodeStateTask(currentNode, NodeDetails.NodeState.Reprovisioning)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);

      Set<NodeDetails> nodeCollection = Collections.singleton(currentNode);

      // Need to reinstall node agent.
      deleteNodeAgent(currentNode);
      createInstallNodeAgentTasks(nodeCollection)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
      createWaitForNodeAgentTasks(nodeCollection)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);

      createSetupServerTasks(nodeCollection, params -> {})
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
      createConfigureServerTasks(nodeCollection, params -> {})
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
      createGFlagsOverrideTasks(nodeCollection, ServerType.MASTER);
      createGFlagsOverrideTasks(nodeCollection, ServerType.TSERVER);

      createSetNodeStateTask(currentNode, NodeDetails.NodeState.Stopped)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);

      // Mark universe update success to true
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future updates to
      // the universe.
      unlockUniverseForUpdate();
    }
  }
}
