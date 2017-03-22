package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
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

  // Upgrade Task Type
  public enum UpgradeTaskType {
    Everything,
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

        if (taskParams().ybSofwareVersion.equals(universe.getUniverseDetails().userIntent.ybSofwareVersion)) {
          throw new RuntimeException("Cluster is already on yugabyte software version: " + taskParams().ybSofwareVersion);
        }

        LOG.info("Upgrading software version to {} for {} nodes in universe {}",
                 taskParams().ybSofwareVersion, taskParams().nodeNames.size(), universe.name);
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
        createSetNodeStateTask(node, NodeDetails.NodeState.UpgradeSoftware);
        createSoftwareUpgradeTask(node, processType);
      } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
        createSetNodeStateTask(node, NodeDetails.NodeState.UpdateGFlags);
        createGFlagsUpgradeTask(node, processType);
      }
      createSetNodeStateTask(node, NodeDetails.NodeState.Running);
    }
  }

  public void createSoftwareUpgradeTask(NodeDetails node, UniverseDefinitionTaskBase.ServerType processType) {
    TaskList taskList = new TaskList("AnsibleConfigureServers (Download Software) for: " + node.nodeName, executor);
    taskList.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Download));
    taskList.setUserSubTask(UserTaskDetails.SubTaskType.DownloadingSoftware);
    taskListQueue.add(taskList);

    createServerControlTask(node, processType, "stop", 0, UserTaskDetails.SubTaskType.DownloadingSoftware);

    taskList = new TaskList("AnsibleConfigureServers (Install Software) for: " + node.nodeName, executor);
    taskList.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Install));
    taskList.setUserSubTask(UserTaskDetails.SubTaskType.InstallingSoftware);
    taskListQueue.add(taskList);
    int sleepMillis = processType == UniverseDefinitionTaskBase.ServerType.MASTER ?
      taskParams().sleepAfterMasterRestartMillis : taskParams().sleepAfterTServerRestartMillis;

    createServerControlTask(node, processType, "start", sleepMillis, UserTaskDetails.SubTaskType.InstallingSoftware);
  }

  public void createGFlagsUpgradeTask(NodeDetails node, UniverseDefinitionTaskBase.ServerType processType) {
    createServerControlTask(node, processType, "stop", 0, UserTaskDetails.SubTaskType.UpdatingGFlags);

    String taskListName = "AnsibleConfigureServers (GFlags Update) for :" + node.nodeName;
    TaskList taskList = new TaskList(taskListName, executor);
    taskList.addTask(getConfigureTask(node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None));
    taskList.setUserSubTask(UserTaskDetails.SubTaskType.UpdatingGFlags);
    taskListQueue.add(taskList);

    int sleepMillis = processType == UniverseDefinitionTaskBase.ServerType.MASTER ?
      taskParams().sleepAfterMasterRestartMillis : taskParams().sleepAfterTServerRestartMillis;
    createServerControlTask(node, processType, "start", sleepMillis, UserTaskDetails.SubTaskType.UpdatingGFlags);
  }

  private AnsibleConfigureServers getConfigureTask(NodeDetails node,
                                                   UniverseDefinitionTaskBase.ServerType processType,
                                                   UpgradeTaskType type,
                                                   UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    // Set the cloud name.
    params.cloud = Common.CloudType.aws;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // Add task type
    params.type = type;
    params.setProperty("processType", processType.toString());
    params.setProperty("taskSubType", taskSubType.toString());

    if (type == UpgradeTaskType.Software) {
      params.ybSofwareVersion = taskParams().ybSofwareVersion;
    } else if (type == UpgradeTaskType.GFlags) {
      params.gflags = taskParams().getGFlagsAsMap();
    }

    if (params.cloud == Common.CloudType.onprem) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    // Create the Ansible task to get the server info.
    AnsibleConfigureServers task = new AnsibleConfigureServers();
    task.initialize(params);

    return task;
  }

}
