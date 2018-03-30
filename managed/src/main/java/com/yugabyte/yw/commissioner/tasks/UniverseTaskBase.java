// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.Collection;
import java.util.List;
import java.util.Map;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupTable;
import com.yugabyte.yw.commissioner.tasks.subtasks.BulkImport;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTable;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteNode;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTableFromUniverse;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeState;
import com.yugabyte.yw.commissioner.tasks.subtasks.SwamperTargetsFileUpdate;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateSucceeded;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistGFlags;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateSoftwareVersion;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.nodes.UpdateNodeProcess;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TableDetails;
import org.yb.Common;

public abstract class UniverseTaskBase extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseTaskBase.class);

  // Flag to indicate if we have locked the universe.
  private boolean universeLocked = false;

  // The task params.
  @Override
  protected UniverseTaskParams taskParams() {
    return (UniverseTaskParams)taskParams;
  }

  private UniverseUpdater getLockingUniverseUpdater(int expectedUniverseVersion,
                                                    boolean checkSuccess) {
    return new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        if (expectedUniverseVersion != -1 && expectedUniverseVersion != universe.version) {
          String msg = "Universe " + taskParams().universeUUID + " version " + universe.version +
            ", is different from the expected version of " + expectedUniverseVersion + ". User " +
            "would have to sumbit the operation from a refreshed top-level universe page.";
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
        if (checkSuccess) {
          universeDetails.updateSucceeded = false;
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
  }

  private Universe lockUniverseForUpdate(int expectedUniverseVersion, UniverseUpdater updater) {
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = Universe.saveDetails(taskParams().universeUUID, updater);
    universeLocked = true;
    LOG.debug("Locked universe {} at version {}.", taskParams().universeUUID,
      expectedUniverseVersion);
    // Return the universe object that we have already updated.
    return universe;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    // Create the threadpool for the subtasks to use.
    createThreadpool();
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
    UniverseUpdater updater = getLockingUniverseUpdater(expectedUniverseVersion, true);
    return lockUniverseForUpdate(expectedUniverseVersion, updater);
  }

  /**
   * Locks the universe by setting the 'updateInProgress' flag. If the universe is already being
   * modified, then throws an exception. Any tasks involving tables should use this method, not any
   * other.
   *
   * @param expectedUniverseVersion Lock only if the current version of the unvierse is at this
   *                                version. -1 implies always lock the universe.
   */
  public Universe lockUniverse(int expectedUniverseVersion) {
    UniverseUpdater updater = getLockingUniverseUpdater(expectedUniverseVersion, false);
    return lockUniverseForUpdate(expectedUniverseVersion, updater);
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
   * Create a task to mark the change on a universe as success.
   */
  public SubTaskGroup createMarkUniverseUpdateSuccessTasks() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("FinalizeUniverseUpdate", executor);
    UniverseUpdateSucceeded.Params params = new UniverseUpdateSucceeded.Params();
    params.universeUUID = taskParams().universeUUID;
    UniverseUpdateSucceeded task = new UniverseUpdateSucceeded();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a task to mark the final software version on a universe.
   */
  public SubTaskGroup createUpdateSoftwareVersionTask(String softwareVersion) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("FinalizeUniverseUpdate", executor);
    UpdateSoftwareVersion.Params params = new UpdateSoftwareVersion.Params();
    params.universeUUID = taskParams().universeUUID;
    params.softwareVersion = softwareVersion;
    UpdateSoftwareVersion task = new UpdateSoftwareVersion();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to destroy nodes and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be destroyed
   */
  public SubTaskGroup createDestroyServerTasks(Collection<NodeDetails> nodes, Boolean isForceDelete) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleDestroyServers", executor);
    for (NodeDetails node : nodes) {
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = taskParams().deviceInfo;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Flag to be set where errors during Ansible Destroy Server will be ignored.
      params.isForceDelete = isForceDelete;
      // Add the instance type
      params.instanceType = node.cloudInfo.instance_type;
      // Create the Ansible task to destroy the server.
      AnsibleDestroyServer task = new AnsibleDestroyServer();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create tasks to update the state of the nodes.
   *
   * @param nodes set of nodes to be updated.
   * @param nodeState State into which these nodes will be transitioned.
   * @return
   */
  public SubTaskGroup createSetNodeStateTasks(Collection<NodeDetails> nodes,
                                              NodeDetails.NodeState nodeState) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("SetNodeState", executor);
    for (NodeDetails node : nodes) {
      SetNodeState.Params params = new SetNodeState.Params();
      params.universeUUID = taskParams().universeUUID;
      params.azUuid = node.azUuid;
      params.nodeName = node.nodeName;
      params.state = nodeState;
      SetNodeState task = new SetNodeState();
      task.initialize(params);
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create task to execute a Cluster CTL command against specific process
   *
   * @param node node for which the CTL command needs to be executed
   * @param processType, Master/TServer process type
   * @param command, actual command (start, stop, create)
   * @param sleepAfterCmdMillis, number of seconds to sleep after the command execution
   * @return SubTaskGroup
   */
  public SubTaskGroup createServerControlTask(NodeDetails node,
                                              UniverseDefinitionTaskBase.ServerType processType,
                                              String command,
                                              int sleepAfterCmdMillis) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    subTaskGroup.addTask(getServerControlTask(node, processType, command, sleepAfterCmdMillis));
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }


  /**
   * Create tasks to execute Cluster CTL command against specific process in parallel
   *
   * @param nodes set of nodes to issue control command in parallel.
   * @param processType, Master/TServer process type
   * @param command, actual command (start, stop, create)
   * @param sleepAfterCmdMillis, number of seconds to sleep after the command execution
   * @return SubTaskGroup
   */
  public SubTaskGroup createServerControlTasks(List<NodeDetails> nodes,
                                               UniverseDefinitionTaskBase.ServerType processType,
                                               String command,
                                               int sleepAfterCmdMillis) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      subTaskGroup.addTask(getServerControlTask(node, processType, command, sleepAfterCmdMillis));
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  private AnsibleClusterServerCtl getServerControlTask(NodeDetails node,
                                                       UniverseDefinitionTaskBase.ServerType processType,
                                                       String command,
                                                       int sleepAfterCmdMillis) {
    AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
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
    // Set the InstanceType
    params.instanceType = node.cloudInfo.instance_type;
    // Create the Ansible task to get the server info.
    AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
    task.initialize(params);
    return task;
  }

  /**
   * Create task to update the state of single node.
   *
   * @param node node for which we need to update the state
   * @param nodeState State into which these nodes will be transitioned.
   */
  public SubTaskGroup createSetNodeStateTask(NodeDetails node, NodeDetails.NodeState nodeState) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("SetNodeState", executor);
    SetNodeState.Params params = new SetNodeState.Params();
    params.azUuid = node.azUuid;
    params.universeUUID = taskParams().universeUUID;
    params.nodeName = node.nodeName;
    params.state = nodeState;
    SetNodeState task = new SetNodeState();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a task to update the swamper target file
   *
   * @param removeFile, flag to state if we want to remove the swamper or not
   * @param subTaskGroupType, user subtask type to use for the SubTaskGroup
   */
  public void createSwamperTargetUpdateTask(boolean removeFile,
                                            UserTaskDetails.SubTaskGroupType subTaskGroupType) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("SwamperTargetFileUpdate", executor);
    SwamperTargetsFileUpdate.Params params = new SwamperTargetsFileUpdate.Params();
    SwamperTargetsFileUpdate task = new SwamperTargetsFileUpdate();
    params.universeUUID = taskParams().universeUUID;
    params.removeFile = removeFile;
    task.initialize(params);
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
  }

  /**
   * Create a task to create a table.
   *
   * @param tableName name of the table.
   * @param tableDetails table options and related  details.
   */
  public SubTaskGroup createTableTask(Common.TableType tableType, String tableName,
                                      TableDetails tableDetails) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("CreateTable", executor);
    CreateTable task = new CreateTable();
    CreateTable.Params params = new CreateTable.Params();
    params.universeUUID = taskParams().universeUUID;
    params.tableType = tableType;
    params.tableName = tableName;
    params.tableDetails = tableDetails;
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a task to delete a table.
   *
   * @param params The necessary parameters for dropping a table.
   */
  public SubTaskGroup createDeleteTableFromUniverseTask(DeleteTableFromUniverse.Params params) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("DeleteTableFromUniverse", executor);
    DeleteTableFromUniverse task = new DeleteTableFromUniverse();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a task list to ping all servers until they are up.
   *
   * @param nodes : a collection of nodes that need to be pinged.
   * @param type  : Master or tserver type server running on these nodes.
   */
  public SubTaskGroup createWaitForServersTasks(Collection<NodeDetails> nodes, ServerType type) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("WaitForServer", executor);
    for (NodeDetails node : nodes) {
      WaitForServer.Params params = new WaitForServer.Params();
      params.universeUUID = taskParams().universeUUID;
      params.nodeName = node.nodeName;
      params.serverType = type;
      WaitForServer task = new WaitForServer();
      task.initialize(params);
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to persist customized gflags to be used by server processes.
   */
  public SubTaskGroup updateGFlagsPersistTasks(Map<String, String> masterGFlags, Map<String, String> tserverGFlags) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("UpdateAndPersistGFlags", executor);
    UpdateAndPersistGFlags.Params params = new UpdateAndPersistGFlags.Params();
    params.universeUUID = taskParams().universeUUID;
    params.masterGFlags = masterGFlags;
    params.tserverGFlags = tserverGFlags;
    UpdateAndPersistGFlags task = new UpdateAndPersistGFlags();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to bulk import data from an s3 bucket into a given table.
   *
   * @param taskParams Info about the table and universe of the table to import into.
   */
  public SubTaskGroup createBulkImportTask(BulkImportParams taskParams) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("BulkImport", executor);
    BulkImport task = new BulkImport();
    task.initialize(taskParams);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup deleteNodeFromUniverseTask(String nodeName) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("RemoveNodeFromUniverse", executor);
    NodeTaskParams params = new NodeTaskParams();
    params.nodeName = nodeName;
    params.universeUUID = taskParams().universeUUID;
    DeleteNode task = new DeleteNode();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Add or Remove Master process on the node
   * @param node the node to add/remove master process on
   * @param isAdd whether Master is being added or removed.
   * @param subTask subtask type
   */
  public void createChangeConfigTask(NodeDetails node,
                                     boolean isAdd,
                                     UserTaskDetails.SubTaskGroupType subTask) {
    // Create a new task list for the change config so that it happens one by one.
    String subtaskGroupName = "ChangeMasterConfig(" + node.nodeName + ", " +
      (isAdd? "add" : "remove") + ")";
    SubTaskGroup subTaskGroup = new SubTaskGroup(subtaskGroupName, executor);
    // Create the task params.
    ChangeMasterConfig.Params params = new ChangeMasterConfig.Params();
    // Set the azUUID
    params.azUuid = node.azUuid;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // This is an add master.
    params.opType = isAdd ? ChangeMasterConfig.OpType.AddMaster :
      ChangeMasterConfig.OpType.RemoveMaster;
    // Create the task.
    ChangeMasterConfig changeConfig = new ChangeMasterConfig();
    changeConfig.initialize(params);
    // Add it to the task list.
    subTaskGroup.addTask(changeConfig);
    // Add the task list to the task queue.
    subTaskGroupQueue.add(subTaskGroup);
    // Configure the user facing subtask for this task list.
    subTaskGroup.setSubTaskGroupType(subTask);
  }

  /**
   * Start T-Server process on the given node
   * @param currentNode the node to operate upon
   * @param taskType Command start/stop
   * @return Subtask group
   */
  public SubTaskGroup createTServerTaskForNode(NodeDetails currentNode, String taskType) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
    // Add the node name.
    params.nodeName = currentNode.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = currentNode.azUuid;
    // The service and the command we want to run.
    params.process = "tserver";
    params.command = taskType;
    // Set the InstanceType
    params.instanceType = currentNode.cloudInfo.instance_type;
    // Create the Ansible task to get the server info.
    AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Wait for Master Leader Election
   * @return subtask group
   */
  public SubTaskGroup createWaitForMasterLeaderTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("WaitForMasterLeader", executor);
    WaitForMasterLeader task = new WaitForMasterLeader();
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Update the node process in Yugaware DB,
   * @param nodeName    : name of the node where the process task is to be updated.
   * @param processType : process type: master or tserver.
   * @param isAdd       : boolean signifying if the process is being added or removed.
   * @return The subtask group.
   */
  public SubTaskGroup createUpdateNodeProcessTask(String nodeName, ServerType processType,
                                                  Boolean isAdd) {
    String subtaskGroupName = "Update Node Process";
    SubTaskGroup subTaskGroup = new SubTaskGroup(subtaskGroupName, executor);
    // Create the task params.
    UpdateNodeProcess.Params params = new UpdateNodeProcess.Params();
    params.processType = processType;
    params.isAdd = isAdd;
    params.universeUUID = taskParams().universeUUID;
    params.nodeName = nodeName;
    UpdateNodeProcess updateNodeProcess = new UpdateNodeProcess();
    updateNodeProcess.initialize(params);
    // Add it to the task list.
    subTaskGroup.addTask(updateNodeProcess);
    // Add the task list to the task queue.
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to start the masters of the cluster to be created and adds it to the task
   * queue.
   * @param nodes   : a collection of nodes that need to be created
   * @param isShell : Determines if the masters should be started in shell mode
   */
  public SubTaskGroup createStartMasterTasks(Collection<NodeDetails> nodes,
                                             boolean isShell) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "master";
      params.command = "start";

      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to stop the masters of the cluster and adds it to the task queue.
   * @param nodes set of nodes to be stopped as master
   * @return
   */
  public SubTaskGroup createStopMasterTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "master";
      params.command = "stop";
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createTableBackupTask(BackupTableParams taskParams) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("BackupTable", executor);
    BackupTable task = new BackupTable();
    task.initialize(taskParams);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

}
