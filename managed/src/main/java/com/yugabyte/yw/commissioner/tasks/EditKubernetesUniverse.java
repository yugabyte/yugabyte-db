// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.HashSet;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.UniverseOpType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLoadBalance;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;

import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class EditKubernetesUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(EditKubernetesUniverse.class);

  @Override
  public void run() {
    try {
      // Verify the task params.
      verifyParams(UniverseOpType.EDIT);

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Get requested user intent.
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      UserIntent userIntent = primaryCluster.userIntent;

      // Get current universe's intent.
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      UserIntent currIntent = universeDetails.getPrimaryCluster().userIntent.clone();

      boolean userIntentChange = false;
      boolean isNumNodeChange = false;
      boolean isFirstIteration = true;

      // Check if number of nodes changed.
      if (currIntent.numNodes != userIntent.numNodes) {
        isNumNodeChange = true;
        currIntent.numNodes = userIntent.numNodes;
      }

      // Check if the requested user intent differs from the universe's current
      // user intent in any fields OTHER than numNodes.
      if (!currIntent.equals(userIntent)) {
          userIntentChange = true;
      }

      // Update the user intent.
      writeUserIntentToUniverse();

      List<NodeDetails> currTServers = universe.getTServers();
      int numTServers = currTServers.size();

      Set<NodeDetails> tserversToRemove = getTServersToRemove(numTServers,
          userIntent.numNodes, universe);
      Set<NodeDetails> tserversToAdd = getTServersToAdd(numTServers,
          userIntent.numNodes, universe);

      // Get node detail set at the start of edit.
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.POD_INFO);  

      if (!tserversToRemove.isEmpty()) {
        createModifyBlackListTask(new ArrayList(tserversToRemove), true /* isAdd */)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        createWaitForDataMoveTask()
            .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
      }

      // Check if rolling pods is not important to reduce tasks to run.
      if (!userIntentChange) {
        createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.HELM_UPGRADE);
      } else {
        // In case user intent changes per pod, roll each pod.
        for (int partition = userIntent.numNodes - 1; partition >= 0; partition--) {
          createKubernetesExecutorTaskForServerType(KubernetesCommandExecutor.CommandType.HELM_UPGRADE,
              null, ServerType.TSERVER, partition);
          createKubernetesWaitForPodTask(KubernetesWaitForPod.CommandType.WAIT_FOR_POD,
              String.format("yb-tserver-%d", partition));

          // Update the node detail set with the current pods so that the wait tasks
          // get the latest pods to wait for (required when new pods are added).
          if (isNumNodeChange && isFirstIteration) {
            createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.POD_INFO);
          }

          // Get the tserver that has just been rolled.
          Set<NodeDetails> tserverUpdated = getTServerToWaitFor(partition);

          // TODO: Ideally should wait for tserver to have locally bootstrapped data.
          createWaitForServersTasks(tserverUpdated, ServerType.TSERVER)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          createWaitForLoadBalanceTask()
              .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

          isFirstIteration = false;
        }
      }

      // Final update for the node details.
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.POD_INFO);

      // Waiting on all newly added tservers.
      if (!tserversToAdd.isEmpty()) {
        createWaitForServersTasks(tserversToAdd, ServerType.TSERVER)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        createWaitForLoadBalanceTask()
            .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
      }

      if (!tserversToRemove.isEmpty()) {
        createModifyBlackListTask(new ArrayList(tserversToRemove), false /* isAdd */)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

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

  public Set<NodeDetails> getTServerToWaitFor(int partition) {
    Set<NodeDetails> tservers = new HashSet<>();
    String tserverName = String.format("yb-tserver-%d", partition);
    NodeDetails node = new NodeDetails();
    node.nodeName = tserverName;
    tservers.add(node);
    return tservers;
  }

  public Set<NodeDetails> getTServersToRemove(int numCurrTServers, int numIntendedTServers,
      Universe universe) {
    Set<NodeDetails> tservers = new HashSet<>();
    for (int i = numIntendedTServers; i < numCurrTServers; i++) {
      String tserverName = String.format("yb-tserver-%d", i);
      tservers.add(universe.getNode(tserverName));
    }
    return tservers;
  }

  public Set<NodeDetails> getTServersToAdd(int numCurrTServers, int numIntendedTServers,
      Universe universe) {
    Set<NodeDetails> tservers = new HashSet<>();
    for (int i = numCurrTServers; i < numIntendedTServers; i++) {
      String tserverName = String.format("yb-tserver-%d", i);
      // Creating a pseudo-NodeDetails object since ModifyBlacklist requires a NodeDetail object
      // and the NodeDetails aren't updated till after the pods have been added.
      NodeDetails node = new NodeDetails();
      node.nodeName = tserverName;
      tservers.add(node);
    }
    return tservers;
  }
}
