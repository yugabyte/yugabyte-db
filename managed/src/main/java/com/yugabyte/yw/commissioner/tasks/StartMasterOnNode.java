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

import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;

// Allows the addition of the master server to a node. Spawns master the process and ensures
// the task waits for the right set of load balance primitives.
public class StartMasterOnNode extends UniverseDefinitionTaskBase {

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    LOG.info("Started {} task for node {} in univ uuid={}", getName(), taskParams().nodeName,
        taskParams().universeUUID);
    NodeDetails currentNode = null;
    boolean hitException = false;
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the DB to prevent other changes from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg = "No node " + taskParams().nodeName + " in universe " + universe.name;
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      if (universe.getMasters().contains(currentNode)) {
        String msg = "Node " + taskParams().nodeName + " already has the Master server running.";
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      if (currentNode.state == NodeState.Removed || currentNode.state == NodeState.Decommissioned) {
        String msg = "Node " + taskParams().nodeName + " is in removed or decommissioned state"
            + ", the Master server cannot be started. Use \"Start Node\" instead.";
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      if (!areMastersUnderReplicated(currentNode, universe)) {
        String msg = "Unable to add the Master server on node " + taskParams().nodeName
            + ", no more Master servers allowed.";
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      // Update node state to Starting Master
      createSetNodeStateTask(currentNode, NodeState.Starting)
          .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

      Collection<NodeDetails> node = new HashSet<NodeDetails>(Arrays.asList(currentNode));

//      // ??????
//      // Re-install software.
//      // TODO: Remove the need for version for existing instance, NodeManger needs
//      // changes.
//      createConfigureServerTasks(node, true /* isShell */)
//          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Bring up any masters, as needed.
      LOG.info("Bringing up master for under replicated universe {} ({})", universe.universeUUID,
          universe.name);

      // TODO: uncomment after submit of #5207
      // Setting up default gflags
      // addDefaultGFlags();
      // Explicitly set webserver ports for each dql
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      primaryCluster.userIntent.tserverGFlags.put("redis_proxy_webserver_port",
          Integer.toString(taskParams().communicationPorts.redisServerHttpPort));
      primaryCluster.userIntent.tserverGFlags.put("cql_proxy_webserver_port",
          Integer.toString(taskParams().communicationPorts.yqlServerHttpPort));
      if (primaryCluster.userIntent.enableYSQL) {
        primaryCluster.userIntent.tserverGFlags.put("pgsql_proxy_webserver_port",
            Integer.toString(taskParams().communicationPorts.ysqlServerHttpPort));
      }

      // Set gflags for master.
      createGFlagsOverrideTasks(node, ServerType.MASTER);

      // Start a shell master process.
      createStartMasterTasks(node).setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Mark node as a master in YW DB.
      // Do this last so that master addresses does not pick up current node.
      createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, true)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Wait for master to be responsive.
      createWaitForServersTasks(node, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Add it into the master quorum.
      createChangeConfigTask(currentNode, true, SubTaskGroupType.WaitForDataMigration);

      // Wait for load to balance.
      createWaitForLoadBalanceTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);

      // Update all server conf files with new master information.
      createMasterInfoUpdateTask(universe, currentNode);

      // Update node state to live.
      createSetNodeStateTask(currentNode, NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      hitException = true;
      throw t;
    } finally {
      // Reset the state, on any failure, so that the actions can be retried.
      if (currentNode != null && hitException) {
        setNodeState(taskParams().nodeName, currentNode.state);
      }

      // Mark the update of the universe as done. This will allow future updates to
      // the universe.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
