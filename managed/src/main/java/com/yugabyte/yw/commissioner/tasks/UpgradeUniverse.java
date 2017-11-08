// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.models.Universe;
import play.libs.Json;

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

  @Override
  protected RollingRestartParams taskParams() {
    return (RollingRestartParams)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      List<NodeDetails> tServerNodes = universe.getTServers();
      List<NodeDetails> masterNodes  = universe.getMasters();

      if (taskParams().taskType == UpgradeTaskType.Software) {
        if (taskParams().ybSoftwareVersion == null || taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException("Invalid yugabyte software version: " + taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(universe.getUniverseDetails().userIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException("Cluster is already on yugabyte software version: " + taskParams().ybSoftwareVersion);
        }

        LOG.info("Upgrading software version to {} in universe {}",
                 taskParams().ybSoftwareVersion, universe.name);
      } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
        LOG.info("Updating Master gflags: {} for {} nodes in universe {}",
          taskParams().masterGFlags, masterNodes.size(), universe.name);
        LOG.info("Updating T-Server gflags: {} for {} nodes in universe {}",
          taskParams().tserverGFlags,  tServerNodes.size(), universe.name);
      }

      SubTaskGroupType subTaskGroupType;
      switch (taskParams().taskType) {
        case Software:
          subTaskGroupType = SubTaskGroupType.DownloadingSoftware;
          // Disable the load balancer.
          createLoadBalancerStateChangeTask(false /*enable*/)
            .setSubTaskGroupType(subTaskGroupType);
          createAllUpgradeTasks(tServerNodes, ServerType.TSERVER); // Implicitly calls setSubTaskGroupType
          // Enable the load balancer.
          createLoadBalancerStateChangeTask(true /*enable*/)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          break;
        case GFlags:
          subTaskGroupType = SubTaskGroupType.UpdatingGFlags;
          if (!taskParams().masterGFlags.isEmpty()) {
            createAllUpgradeTasks(masterNodes, ServerType.MASTER); // Implicitly calls setSubTaskGroupType
          }
          if (!taskParams().tserverGFlags.isEmpty()) {
            // Disable the load balancer.
            createLoadBalancerStateChangeTask(false /*enable*/)
              .setSubTaskGroupType(subTaskGroupType);
            createAllUpgradeTasks(tServerNodes, ServerType.TSERVER); // Implicitly calls setSubTaskGroupType
            // Enable the load balancer.
            createLoadBalancerStateChangeTask(true /*enable*/)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          }
      }

      // Update the software version on success.
      if (taskParams().taskType == UpgradeTaskType.Software) {
        createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Update the list of parameter key/values in the universe with the new ones.
      if (taskParams().taskType == UpgradeTaskType.GFlags) {
        updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
            .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      }

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
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
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServers (Download Software) for: " + node.nodeName, executor);
    subTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Download));
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    subTaskGroupQueue.add(subTaskGroup);

    createServerControlTask(node, processType, "stop", 0, SubTaskGroupType.DownloadingSoftware);

    subTaskGroup = new SubTaskGroup("AnsibleConfigureServers (Install Software) for: " + node.nodeName, executor);
    subTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Install));
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
    subTaskGroupQueue.add(subTaskGroup);

    createServerControlTask(node, processType, "start", getSleepTimeForProcess(processType),
                            SubTaskGroupType.InstallingSoftware);
  }

  private void createGFlagsUpgradeTask(NodeDetails node, ServerType processType) {
    createServerControlTask(node, processType, "stop", 0, SubTaskGroupType.UpdatingGFlags);

    String subTaskGroupName = "AnsibleConfigureServers (GFlags Update) for :" + node.nodeName;
    SubTaskGroup subTaskGroup = new SubTaskGroup(subTaskGroupName, executor);
    subTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None));
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    subTaskGroupQueue.add(subTaskGroup);
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
    params.deviceInfo = Universe.get(taskParams().universeUUID).getUniverseDetails().userIntent.deviceInfo;
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
      if (processType.equals(ServerType.MASTER)) {
        params.gflags = taskParams().masterGFlags;
      } else {
        params.gflags = taskParams().tserverGFlags;
      }
    }

    if (params.cloud == Common.CloudType.onprem) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    // Create the Ansible task to get the server info.
    AnsibleConfigureServers task = new AnsibleConfigureServers();
    task.initialize(params);

    return task;
  }

  private SubTaskGroup createLoadBalancerStateChangeTask(boolean enable) {
    LoadBalancerStateChange.Params params = new LoadBalancerStateChange.Params();
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    params.enable = enable;
    LoadBalancerStateChange task = new LoadBalancerStateChange();
    task.initialize(params);

    SubTaskGroup subTaskGroup = new SubTaskGroup("LoadBalancerStateChange", executor);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }
}
