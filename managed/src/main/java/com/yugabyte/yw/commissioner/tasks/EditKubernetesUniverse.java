// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.Universe;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common;
import org.yb.client.YBClient;

import java.util.Set;
import java.util.UUID;

public class EditKubernetesUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(EditKubernetesUniverse.class);

  @Override
  public void run() {
    try {
      // Verify the task params.
      verifyParams();
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      
      UniverseDefinitionTaskParams.UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

      UUID providerUUID = UUID.fromString(userIntent.provider);

      // Update the user intent.
      writeUserIntentToUniverse();

      // Set the correct node names as they are finalized now. This is done just in case the user
      // changes the universe name before submitting.
      updateNodeNames();

      // Run the kubectl command to change the number of replicas.
      createEditKubernetesExecutorTask(providerUUID,
          universe.getUniverseDetails().nodePrefix,
          KubernetesCommandExecutor.CommandType.UPDATE_NUM_NODES);
      
      // Wait for pods to be ready after deployment.
      createKubernetesWaitForPodTask(providerUUID,
          universe.getUniverseDetails().nodePrefix,
          KubernetesWaitForPod.CommandType.WAIT_FOR_POD);
      
      createEditKubernetesExecutorTask(providerUUID,
          universe.getUniverseDetails().nodePrefix,
          KubernetesCommandExecutor.CommandType.POD_INFO);

      createSwamperTargetUpdateTask(false);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

  private void createEditKubernetesExecutorTask(UUID providerUUID, String nodePrefix,
                                             KubernetesCommandExecutor.CommandType commandType) {
    SubTaskGroup subTaskGroup = new SubTaskGroup(commandType.getSubTaskGroupName(), executor);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.providerUUID = providerUUID;
    params.commandType = commandType;
    params.nodePrefix = nodePrefix;
    params.universeUUID = taskParams().universeUUID;
    KubernetesCommandExecutor task = new KubernetesCommandExecutor();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
  }

  private void createKubernetesWaitForPodTask(UUID providerUUID, String nodePrefix,
                                             KubernetesWaitForPod.CommandType commandType) {
    SubTaskGroup subTaskGroup = new SubTaskGroup(commandType.getSubTaskGroupName(), executor);
    Universe u = Universe.get(taskParams().universeUUID);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent;
    for (int i = 0; i < userIntent.numNodes; i++) {
      KubernetesWaitForPod.Params params = new KubernetesWaitForPod.Params();
      params.providerUUID = providerUUID;
      params.commandType = commandType;
      params.nodePrefix = nodePrefix;
      params.universeUUID = taskParams().universeUUID;
      params.podName = "yb-tserver-" + i;
      KubernetesWaitForPod task = new KubernetesWaitForPod();
      task.initialize(params);
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.KubernetesWaitForPod);
  }  
}
