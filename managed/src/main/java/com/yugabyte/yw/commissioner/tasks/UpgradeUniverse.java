package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpgradeServer;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.models.Universe;

import java.util.List;
import java.util.function.Predicate;
import java.util.stream.Collectors;

public class UpgradeUniverse extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpgradeUniverse.class);

  // Upgrade task type
  public enum UpgradeTaskType {
    Software,
    GFlags
  }

  public enum UpgradeTaskSubType {
    None,
    Download,
    Install
  }

  public static class Params extends RollingRestartParams {}

  public static List<NodeDetails> filterByNodeName (List<NodeDetails> nodeDetails, List<String> nodeNames) {
    if (nodeNames.isEmpty()) {
      return nodeDetails;
    }

    Predicate<NodeDetails> nodeNameFilter = p -> nodeNames.contains(p.nodeName);
    return nodeDetails.stream().filter(nodeNameFilter).collect(Collectors.toList());
  }

  @Override
  protected RollingRestartParams taskParams() { return (RollingRestartParams)taskParams; }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      taskListQueue = new TaskListQueue();

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      if (taskParams().taskType == UpgradeTaskType.Software) {
        LOG.info("Upgrading software version to {} for {} nodes in universe {}",
                 taskParams().ybServerPkg, taskParams().nodeNames.size(), universe.name);
      } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
        LOG.info("Updating gflags: {} for {} nodes in universe {}",
                 taskParams().getGFlagsAsMap(), taskParams().nodeNames.size(), universe.name);
      }

      if (taskParams().upgradeMasters) {
        createUpgradeTasks(universe.getMasters(), UniverseDefinitionTaskBase.ServerType.MASTER);
      }

      if (taskParams().upgradeTServers) {
        createUpgradeTasks(universe.getTServers(), UniverseDefinitionTaskBase.ServerType.TSERVER);
      }

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error={}.", getName(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

  public void createUpgradeTasks(List<NodeDetails> nodes, UniverseDefinitionTaskBase.ServerType processType) {
    nodes = filterByNodeName(nodes, taskParams().nodeNames);
    for (NodeDetails node : nodes) {
      if (taskParams().taskType == UpgradeTaskType.Software) {
        createSoftwareUpgradeTask(node, processType);
      } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
        createGFlagsUpgradeTask(node, processType);
      }
    }
  }

  public void createSoftwareUpgradeTask(NodeDetails node, UniverseDefinitionTaskBase.ServerType processType) {
    TaskList taskList = new TaskList("AnsibleUpgradeServer (Download Software) for: " + node.nodeName, executor);
    taskList.addTask(getTask(node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Download));
    taskList.setUserSubTask(UserTaskDetails.SubTaskType.DownloadingSoftware);
    taskListQueue.add(taskList);

    createServerControlTask(node, processType, "stop", 0, UserTaskDetails.SubTaskType.DownloadingSoftware);

    taskList = new TaskList("AnsibleUpgradeServer (Install Software) for: " + node.nodeName, executor);
    taskList.addTask(getTask(node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Install));
    taskList.setUserSubTask(UserTaskDetails.SubTaskType.InstallingSoftware);
    taskListQueue.add(taskList);
    int sleepMillis = processType == UniverseDefinitionTaskBase.ServerType.MASTER ?
      taskParams().sleepAfterMasterRestartMillis : taskParams().sleepAfterTServerRestartMillis;

    createServerControlTask(node, processType, "start", sleepMillis, UserTaskDetails.SubTaskType.InstallingSoftware);
  }

  public void createGFlagsUpgradeTask(NodeDetails node, UniverseDefinitionTaskBase.ServerType processType) {
    createServerControlTask(node, processType, "stop", 0, UserTaskDetails.SubTaskType.UpdatingGFlags);

    String taskListName = "AnsibleUpgradeServer (GFlags Update) for :" + node.nodeName;
    TaskList taskList = new TaskList(taskListName, executor);
    taskList.addTask(getTask(node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None));
    taskList.setUserSubTask(UserTaskDetails.SubTaskType.UpdatingGFlags);
    taskListQueue.add(taskList);

    int sleepMillis = processType == UniverseDefinitionTaskBase.ServerType.MASTER ?
      taskParams().sleepAfterMasterRestartMillis : taskParams().sleepAfterTServerRestartMillis;
    createServerControlTask(node, processType, "start", sleepMillis, UserTaskDetails.SubTaskType.UpdatingGFlags);
  }

  private AnsibleUpgradeServer getTask(NodeDetails node,
                                       UniverseDefinitionTaskBase.ServerType processType,
                                       UpgradeTaskType taskType,
                                       UpgradeTaskSubType taskSubType) {
    AnsibleUpgradeServer.Params params = new AnsibleUpgradeServer.Params();
    // Set the cloud name.
    params.cloud = Common.CloudType.aws;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // Add task type
    params.taskType =  taskType;
    params.setProperty("processType", processType.toString());
    params.setProperty("taskSubType", taskSubType.toString());

    if (taskType == UpgradeTaskType.Software) {
      params.ybServerPkg = taskParams().ybServerPkg;
    } else if (taskType == UpgradeTaskType.GFlags) {
      params.gflags = taskParams().getGFlagsAsMap();
    }

    // Create the Ansible task to get the server info.
    AnsibleUpgradeServer task = new AnsibleUpgradeServer();
    task.initialize(params);

    return task;
  }

}
