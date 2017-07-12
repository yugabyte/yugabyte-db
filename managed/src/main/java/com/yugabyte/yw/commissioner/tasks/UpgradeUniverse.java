// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
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
  protected RollingRestartParams taskParams() {
    return (RollingRestartParams)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      taskListQueue = new TaskListQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      if (taskParams().taskType == UpgradeTaskType.Software) {
        if (taskParams().ybSoftwareVersion == null || taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException("Invalid yugabyte software version: " + taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(universe.getUniverseDetails().userIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException("Cluster is already on yugabyte software version: " + taskParams().ybSoftwareVersion);
        }

        LOG.info("Upgrading software version to {} for {} nodes in universe {}",
                 taskParams().ybSoftwareVersion, taskParams().nodeNames.size(), universe.name);
      } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
        LOG.info("Updating gflags: {} for {} nodes in universe {}",
                 taskParams().getGFlagsAsMap(), taskParams().nodeNames.size(), universe.name);
      }

      if (taskParams().upgradeMasters) {
        List<NodeDetails> nodes = filterByNodeName(universe.getMasters(), taskParams().nodeNames);
        createAllUpgradeTasks(nodes, ServerType.MASTER); // Implicitly calls setSubTaskGroupType
      }

      if (taskParams().upgradeTServers) {
        SubTaskGroupType subTaskGroupType;
        switch (taskParams().taskType) {
          case Software:
            subTaskGroupType = SubTaskGroupType.DownloadingSoftware;
            break;
          case GFlags:
            subTaskGroupType = SubTaskGroupType.UpdatingGFlags;
            break;
          default:
            subTaskGroupType = SubTaskGroupType.Invalid;
            break;
        }

        // Disable the load balancer.
        createLoadBalancerStateChangeTask(false /*enable*/)
            .setSubTaskGroupType(subTaskGroupType);

        List<NodeDetails> nodes = filterByNodeName(universe.getTServers(), taskParams().nodeNames);
        createAllUpgradeTasks(nodes, ServerType.TSERVER); // Implicitly calls setSubTaskGroupType

        // Enable the load balancer.
        createLoadBalancerStateChangeTask(true /*enable*/)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Update the software version on success.
      if (taskParams().taskType == UpgradeTaskType.Software) {
        createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Update the list of parameter key/values in the universe with the new ones.
      if (taskParams().taskType == UpgradeTaskType.GFlags) {
        updateGFlagsPersistTasks(taskParams().getGFlagsAsMap())
            .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      }

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

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

  private void createAllUpgradeTasks(List<NodeDetails> servers,
                                     ServerType processType) {
    createUpgradeTasks(servers, processType);
    createWaitForServersTasks(servers, processType)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  // The first node state change should be done per node to show the rolling upgrades in action.
  private void createUpgradeTasks(List<NodeDetails> nodes, ServerType processType) {
    for (NodeDetails node : nodes) {
      SubTaskGroupType subTaskGroupType;
      switch (taskParams().taskType) {
        case Software:
          subTaskGroupType = SubTaskGroupType.DownloadingSoftware;
          createSetNodeStateTask(node, NodeDetails.NodeState.UpgradeSoftware)
              .setSubTaskGroupType(subTaskGroupType);
          createSoftwareUpgradeTask(node, processType); // Implicitly calls setSubTaskGroupType
          break;
        case GFlags:
          subTaskGroupType = SubTaskGroupType.UpdatingGFlags;
          createSetNodeStateTask(node, NodeDetails.NodeState.UpdateGFlags)
              .setSubTaskGroupType(subTaskGroupType);
          createGFlagsUpgradeTask(node, processType);
          break;
        default:
          subTaskGroupType = SubTaskGroupType.Invalid;
          break;
      }
      createSetNodeStateTask(node, NodeDetails.NodeState.Running)
          .setSubTaskGroupType(subTaskGroupType);
    }
  }

  private void createSoftwareUpgradeTask(NodeDetails node, ServerType processType) {
    TaskList taskList = new TaskList("AnsibleConfigureServers (Download Software) for: " + node.nodeName, executor);
    taskList.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Download));
    taskList.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    taskListQueue.add(taskList);

    createServerControlTask(node, processType, "stop", 0, SubTaskGroupType.DownloadingSoftware);

    taskList = new TaskList("AnsibleConfigureServers (Install Software) for: " + node.nodeName, executor);
    taskList.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Install));
    taskList.setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
    taskListQueue.add(taskList);

    createServerControlTask(node, processType, "start", getSleepTimeForProcess(processType),
                            SubTaskGroupType.InstallingSoftware);
  }

  private void createGFlagsUpgradeTask(NodeDetails node, ServerType processType) {
    createServerControlTask(node, processType, "stop", 0, SubTaskGroupType.UpdatingGFlags);

    String taskListName = "AnsibleConfigureServers (GFlags Update) for :" + node.nodeName;
    TaskList taskList = new TaskList(taskListName, executor);
    taskList.addTask(getConfigureTask(node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None));
    taskList.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    taskListQueue.add(taskList);
    createServerControlTask(node, processType, "start", getSleepTimeForProcess(processType),
                            SubTaskGroupType.UpdatingGFlags);
  }

  private int getSleepTimeForProcess(ServerType processType) {
    return processType == ServerType.MASTER ?
               taskParams().sleepAfterMasterRestartMillis : taskParams().sleepAfterTServerRestartMillis;
  }

  private AnsibleConfigureServers getConfigureTask(NodeDetails node,
                                                   ServerType processType,
                                                   UpgradeTaskType type,
                                                   UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    // Set the cloud name.
    params.cloud = Common.CloudType.valueOf(node.cloudInfo.cloud);
    // Set the device information (numVolumes, volumeSize, etc.)
    params.deviceInfo = taskParams().userIntent.deviceInfo;
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
      params.ybSoftwareVersion = taskParams().ybSoftwareVersion;
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

  private TaskList createLoadBalancerStateChangeTask(boolean enable) {
    LoadBalancerStateChange.Params params = new LoadBalancerStateChange.Params();
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    params.enable = enable;
    LoadBalancerStateChange task = new LoadBalancerStateChange();
    task.initialize(params);

    TaskList taskList = new TaskList("LoadBalancerStateChange", executor);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }
}
