// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.Collection;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeState;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;

public abstract class UniverseTaskBase extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseTaskBase.class);

  // Flag to indicate if we have locked the universe.
  private boolean universeLocked = false;

  // The task params.
  @Override
  protected UniverseTaskParams taskParams() {
    return (UniverseTaskParams)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    // Create the threadpool for the subtasks to use.
    createThreadpool(10);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  /**
   * Locks the universe for updates by setting the 'updateInProgress' flag. If the universe is
   * already being modified, then throws an exception.
   *
   * @param expectedUniverseVersion Lock only if the current version of the universe is at this
   *                                version. -1 implies always lock the universe.
   */
  public Universe lockUniverseForUpdate(int expectedUniverseVersion) {
    // Create the update lambda.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        if (expectedUniverseVersion != -1 &&
            expectedUniverseVersion != universe.version) {
          String msg = "Universe " + taskParams().universeUUID + " version " + universe.version +
                       ", is different from the expected version of " + expectedUniverseVersion;
          LOG.error(msg);
          throw new IllegalStateException(msg);
        }

        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

        // If this universe is already being edited, fail the request.
        if (universeDetails.updateInProgress) {
          String msg = "UserUniverse " + taskParams().universeUUID + " is already being updated.";
          LOG.error(msg);
          throw new RuntimeException(msg);
        }

        // Persist the updated information about the universe. Mark it as being edited.
        universeDetails.updateInProgress = true;
        universeDetails.updateSucceeded = false;
        universe.setUniverseDetails(universeDetails);
      }
    };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = Universe.saveDetails(taskParams().universeUUID, updater);
    universeLocked = true;
    LOG.debug("Locked universe {} at version {}.",
              taskParams().universeUUID, expectedUniverseVersion);
    // Return the universe object that we have already updated.
    return universe;
  }

  public void unlockUniverseForUpdate() {
    if (!universeLocked) {
      LOG.warn("Unlock universe called when it was not locked.");
      return;
    }
    // Create the update lambda.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        // If this universe is not being edited, fail the request.
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        if (!universeDetails.updateInProgress) {
          String msg = "UserUniverse " + taskParams().universeUUID + " is not being edited.";
          LOG.error(msg);
          throw new RuntimeException(msg);
        }
        // Persist the updated information about the universe. Mark it as being edited.
        universeDetails.updateInProgress = false;
        universe.setUniverseDetails(universeDetails);
      }
    };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.debug("Unlocked universe {} for updates.", taskParams().universeUUID);
  }

  /**
   * Creates a task list to destroy nodes and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be destroyed
   */
  public TaskList createDestroyServerTasks(Collection<NodeDetails> nodes) {
    TaskList taskList = new TaskList("AnsibleDestroyServers", executor);
    for (NodeDetails node : nodes) {
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      // Set the cloud name.
      params.cloud = taskParams().cloud;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Create the Ansible task to destroy the server.
      AnsibleDestroyServer task = new AnsibleDestroyServer();
      task.initialize(params);
      // Add it to the task list.
      taskList.addTask(task);
    }
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Create tasks to update the state of the nodes.
   *
   * @param nodes set of nodes to be updated.
   * @param nodeState State into which these nodes will be transitioned.
   * @return
   */
  public TaskList createSetNodeStateTasks(Collection<NodeDetails> nodes,
                                          NodeDetails.NodeState nodeState) {
    TaskList taskList = new TaskList("SetNodeState", executor);
    for (NodeDetails node : nodes) {
      SetNodeState.Params params = new SetNodeState.Params();
      params.universeUUID = taskParams().universeUUID;
      params.nodeName = node.nodeName;
      params.state = nodeState;
      SetNodeState task = new SetNodeState();
      task.initialize(params);
      taskList.addTask(task);
    }
    taskListQueue.add(taskList);
    return taskList;
  }

  /**
   * Create task to execute a Cluster CTL command against specific process
   *
   * @param node node for which the CTL command needs to be executed
   * @param processType, Master/TServer process type
   * @param command, actual command (start, stop, create)
   * @param sleepAfterCmdMillis, number of seconds to sleep after the command execution
   * @param subTask, user subtask type to use for the tasklist
   */
  public void createServerControlTask(NodeDetails node,
                                      UniverseDefinitionTaskBase.ServerType processType,
                                      String command,
                                      int sleepAfterCmdMillis,
                                      UserTaskDetails.SubTaskType subTask) {
    TaskList taskList = new TaskList("AnsibleClusterServerCtl", executor);
    AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
    // Set the cloud name.
    params.cloud = CloudType.aws;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // The service and the command we want to run.
    params.process = processType.toString().toLowerCase();
    params.command = command;
    params.sleepAfterCmdMills = sleepAfterCmdMillis;
    // Create the Ansible task to get the server info.
    AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
    task.initialize(params);
    // Add it to the task list.
    taskList.setUserSubTask(subTask);
    taskList.addTask(task);

    taskListQueue.add(taskList);
  }

  /**
   * Create task to update the state of single node.
   *
   * @param node node for which we need to update the state
   * @param nodeState State into which these nodes will be transitioned.
   */
  public void createSetNodeStateTask(NodeDetails node, NodeDetails.NodeState nodeState) {
    TaskList taskList = new TaskList("SetNodeState", executor);
    SetNodeState.Params params = new SetNodeState.Params();
    params.universeUUID = taskParams().universeUUID;
    params.nodeName = node.nodeName;
    params.state = nodeState;
    SetNodeState task = new SetNodeState();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
  }
}
