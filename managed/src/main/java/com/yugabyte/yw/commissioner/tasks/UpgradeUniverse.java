// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.models.Universe;

import java.util.List;

import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpgradeSoftware;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateGFlags;

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
        if (taskParams().ybSoftwareVersion == null ||
            taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException("Invalid yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(
                universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException("Cluster is already on yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
      }

      // TODO: we need to fix this, right now if the gflags is empty on both master and tserver
      // we don't update the nodes properly but we do wipe the data from the backend (postgres).
      // JIRA ENG-2519 would track this.
      boolean didUpgradeUniverse = false;
      switch (taskParams().taskType) {
        case Software:
          LOG.info("Upgrading software version to {} in universe {}",
                   taskParams().ybSoftwareVersion, universe.name);
          // TODO: This is assuming that master nodes is a subset of tserver node, instead we should do a union
          createDownloadTasks(tServerNodes);
          // Disable the load balancer for rolling upgrade.
          if (taskParams().rollingUpgrade) {
            createLoadBalancerStateChangeTask(false /*enable*/)
                .setSubTaskGroupType(getTaskSubGroupType());
          }

          createAllUpgradeTasks(masterNodes, ServerType.MASTER);
          createAllUpgradeTasks(tServerNodes, ServerType.TSERVER);
          // Enable the load balancer for rolling upgrade only.
          if (taskParams().rollingUpgrade) {
            createLoadBalancerStateChangeTask(true /*enable*/)
                .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          }
          didUpgradeUniverse = true;
          break;
        case GFlags:
          if (!taskParams().masterGFlags.isEmpty()) {
            LOG.info("Updating Master gflags: {} for {} nodes in universe {}",
                taskParams().masterGFlags, masterNodes.size(), universe.name);
            createAllUpgradeTasks(masterNodes, ServerType.MASTER);
            didUpgradeUniverse = true;
          }
          if (!taskParams().tserverGFlags.isEmpty()) {
            LOG.info("Updating T-Server gflags: {} for {} nodes in universe {}",
                taskParams().tserverGFlags,  tServerNodes.size(), universe.name);
            // Disable the load balancer for rolling upgrade.
            if (taskParams().rollingUpgrade) {
              createLoadBalancerStateChangeTask(false /*enable*/)
                  .setSubTaskGroupType(getTaskSubGroupType());
            }
            createAllUpgradeTasks(tServerNodes, ServerType.TSERVER);
            // Enable the load balancer for rolling upgrade only.
            if (taskParams().rollingUpgrade) {
              createLoadBalancerStateChangeTask(true /*enable*/)
                .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
            }
            didUpgradeUniverse = true;
          }
      }

      if (didUpgradeUniverse) {
        if (taskParams().taskType == UpgradeTaskType.GFlags) {
          // Update the list of parameter key/values in the universe with the new ones.
          updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
              .setSubTaskGroupType(getTaskSubGroupType());
        } else if (taskParams().taskType == UpgradeTaskType.Software) {
          // Update the software version on success.
          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
        }

        // Marks update of this universe as a success only if all the tasks before it succeeded.
        createMarkUniverseUpdateSuccessTasks()
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
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

  private void createAllUpgradeTasks(List<NodeDetails> nodes,
                                     ServerType processType) {

    if (taskParams().rollingUpgrade) {
      for (NodeDetails node : nodes) {
        createNodeUpgradeTask(node, processType);
      }
    } else {
      createNodeUpgradeTasks(nodes, processType);
    }

    createWaitForServersTasks(nodes, processType)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  private void createDownloadTasks(List<NodeDetails> nodes) {
    String subGroupDescription = String.format("AnsibleConfigureServers (%s) for: %s",
        SubTaskGroupType.DownloadingSoftware, taskParams().nodePrefix);
    SubTaskGroup downloadTaskGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      downloadTaskGroup.addTask(getConfigureTask(node, ServerType.TSERVER,
                                UpgradeTaskType.Software, UpgradeTaskSubType.Download));
    }
    downloadTaskGroup.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    subTaskGroupQueue.add(downloadTaskGroup);
  }

  private void createNodeUpgradeTask(NodeDetails node, ServerType processType) {
    NodeDetails.NodeState nodeState = taskParams().taskType == UpgradeTaskType.Software
        ? UpgradeSoftware : UpdateGFlags;
    createSetNodeStateTask(node, nodeState).setSubTaskGroupType(getTaskSubGroupType());
    createServerControlTask(node, processType, "stop", 0)
        .setSubTaskGroupType(getTaskSubGroupType());
    if (taskParams().taskType == UpgradeTaskType.Software) {
      SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServers (Software) for: " +
                                                   node.nodeName, executor);
      subTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software,
                                            UpgradeTaskSubType.Install));
      subTaskGroup.setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      subTaskGroupQueue.add(subTaskGroup);
    } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
      String subTaskGroupName = "AnsibleConfigureServers (GFlags) for :" + node.nodeName;
      SubTaskGroup subTaskGroup = new SubTaskGroup(subTaskGroupName, executor);
      subTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.GFlags,
                                            UpgradeTaskSubType.None));
      subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      subTaskGroupQueue.add(subTaskGroup);
    }

    createServerControlTask(node, processType, "start", getSleepTimeForProcess(processType))
        .setSubTaskGroupType(getTaskSubGroupType());
    createSetNodeStateTask(node, NodeDetails.NodeState.Live)
        .setSubTaskGroupType(getTaskSubGroupType());
  }

  private void createNodeUpgradeTasks(List<NodeDetails> nodes, ServerType processType) {
    NodeDetails.NodeState nodeState = taskParams().taskType == UpgradeTaskType.Software ?
        UpgradeSoftware : UpdateGFlags;
    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(getTaskSubGroupType());
    String subGroupDescription = null;
    if (taskParams().taskType == UpgradeTaskType.Software) {
      createServerControlTasks(nodes, processType, "stop", 0)
          .setSubTaskGroupType(getTaskSubGroupType());
      subGroupDescription = String.format("AnsibleConfigureServers (%s) for: %s",
          SubTaskGroupType.InstallingSoftware, taskParams().nodePrefix);
      SubTaskGroup installTaskGroup =  new SubTaskGroup(subGroupDescription, executor);
      for (NodeDetails node : nodes) {
        installTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software,
                                                  UpgradeTaskSubType.Install));
      }
      installTaskGroup.setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      subTaskGroupQueue.add(installTaskGroup);
    } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
      createServerControlTasks(nodes, processType, "stop", 0)
          .setSubTaskGroupType(getTaskSubGroupType());
      subGroupDescription = String.format("AnsibleConfigureServers (%s) for: %s",
          SubTaskGroupType.UpdatingGFlags, taskParams().nodePrefix);
      SubTaskGroup gFlagsTaskGroup = new SubTaskGroup(subGroupDescription, executor);
      for (NodeDetails node : nodes) {
        gFlagsTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.GFlags,
                                                 UpgradeTaskSubType.None));
      }
      gFlagsTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      subTaskGroupQueue.add(gFlagsTaskGroup);
    }

    createServerControlTasks(nodes, processType, "start", 0)
        .setSubTaskGroupType(getTaskSubGroupType());
    createSetNodeStateTasks(nodes, NodeDetails.NodeState.Live)
        .setSubTaskGroupType(getTaskSubGroupType());
  }

  private SubTaskGroupType getTaskSubGroupType() {
    switch (taskParams().taskType) {
      case Software:
        return SubTaskGroupType.UpgradingSoftware;
      case GFlags:
        return SubTaskGroupType.UpdatingGFlags;
      default:
        return SubTaskGroupType.Invalid;
    }
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
    UserIntent userIntent = Universe.get(taskParams().universeUUID).getUniverseDetails()
        .getClusterByUuid(node.placementUuid).userIntent;
    // Set the device information (numVolumes, volumeSize, etc.)
    params.deviceInfo = userIntent.deviceInfo;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // Add in the node placement uuid.
    params.placementUuid = node.placementUuid;
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

    if (userIntent.providerType.equals(Common.CloudType.onprem)) {
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
