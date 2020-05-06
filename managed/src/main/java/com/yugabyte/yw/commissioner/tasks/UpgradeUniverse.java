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

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;

import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpgradeSoftware;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateGFlags;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collector;
import java.util.stream.Collectors;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

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

      UserIntent primIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
      if (taskParams().taskType == UpgradeTaskType.Software) {
        if (taskParams().ybSoftwareVersion == null ||
            taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException("Invalid yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(primIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException("Cluster is already on yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
      }

      // TODO: we need to fix this, right now if the gflags is empty on both master and tserver
      // we don't update the nodes properly but we do wipe the data from the backend (postgres).
      // JIRA ENG-2519 would track this.
      boolean didUpgradeUniverse = false;
      // Retrieve master leader address of given universe
      final String leaderMasterAddress = universe.getMasterLeaderHostText();
      NodeDetails masterLeaderNode = null;
      switch (taskParams().taskType) {
        case Software:
          LOG.info("Upgrading software version to {} in universe {}",
                   taskParams().ybSoftwareVersion, universe.name);
          // TODO: This is assuming that master nodes is a subset of tserver node, instead we should do a union
          createDownloadTasks(tServerNodes);

          if (taskParams().rollingUpgrade) {
            // Disable the load balancer for rolling upgrade.
            createLoadBalancerStateChangeTask(false /*enable*/)
                .setSubTaskGroupType(getTaskSubGroupType());

            if (!leaderMasterAddress.isEmpty()) {
              // Attempt to isolate the master leader node from the other masters to ensure
              // that it is upgraded last amongst master nodes
              masterLeaderNode = masterNodes
                      .stream()
                      .filter(node -> node.cloudInfo.private_ip.equals(leaderMasterAddress))
                      .findFirst()
                      .orElse(null);
              if (masterLeaderNode != null) {
                masterNodes.removeIf(node -> node.cloudInfo.private_ip.equals(leaderMasterAddress));
              }
            }
            createAllUpgradeTasks(masterNodes, ServerType.MASTER);
            if (masterLeaderNode != null) {
              createSingleNodeUpgradeTasks(masterLeaderNode, ServerType.MASTER);
            }
            createAllUpgradeTasks(tServerNodes, ServerType.TSERVER);
            // Enable the load balancer for rolling upgrade only.
            createLoadBalancerStateChangeTask(true /*enable*/)
                    .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          } else {
            createAllUpgradeTasks(masterNodes, ServerType.MASTER);
            createAllUpgradeTasks(tServerNodes, ServerType.TSERVER);
          }

          didUpgradeUniverse = true;
          break;
        case GFlags:
          if (!taskParams().masterGFlags.equals(primIntent.masterGFlags)) {
            LOG.info("Updating Master gflags: {} for {} nodes in universe {}",
                taskParams().masterGFlags, masterNodes.size(), universe.name);
            if (!taskParams().rollingUpgrade) {
              createServerConfFileUpdateTasks(masterNodes, ServerType.MASTER);
            } else if (!leaderMasterAddress.isEmpty()) {
              // Attempt to isolate the master leader node from the other masters to ensure
              // that it is upgraded last amongst master nodes
              masterLeaderNode = masterNodes
                      .stream()
                      .filter(node -> node.cloudInfo.private_ip.equals(leaderMasterAddress))
                      .findFirst()
                      .orElse(null);
              if (masterLeaderNode != null) {
                masterNodes.removeIf(node -> node.cloudInfo.private_ip.equals(leaderMasterAddress));
              }
            }
            createAllUpgradeTasks(masterNodes, ServerType.MASTER);
            if (masterLeaderNode != null) {
              createSingleNodeUpgradeTasks(masterLeaderNode, ServerType.MASTER);
            }
            didUpgradeUniverse = true;
          }
          if (!taskParams().tserverGFlags.equals(primIntent.tserverGFlags)) {
            LOG.info("Updating T-Server gflags: {} for {} nodes in universe {}",
                taskParams().tserverGFlags, tServerNodes.size(), universe.name);
            if (taskParams().rollingUpgrade) {
              // Disable the load balancer for rolling upgrade.
              createLoadBalancerStateChangeTask(false /*enable*/)
                  .setSubTaskGroupType(getTaskSubGroupType());
            } else {
              // Update conf files only when doing non-rolling upgrade.
              createServerConfFileUpdateTasks(tServerNodes, ServerType.TSERVER);
            }
            createAllUpgradeTasks(tServerNodes, ServerType.TSERVER);
            // Enable the load balancer for rolling upgrade only.
            if (taskParams().rollingUpgrade) {
              createLoadBalancerStateChangeTask(true /*enable*/)
                  .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
            }
            didUpgradeUniverse = true;
          }
          break;
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
      }

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error={}.", getName(), t);

      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
      // If the task failed, we don't want the loadbalancer to be disabled,
      // so we enable it again in case of errors.
      if (taskParams().rollingUpgrade) {
        createLoadBalancerStateChangeTask(true /*enable*/)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
      subTaskGroupQueue.run();
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
        createSingleNodeUpgradeTasks(node, processType);
      }
    } else {
      createMultipleNodeUpgradeTasks(nodes, processType);
      createWaitForServersTasks(nodes, processType)
         .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
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

  private void createServerConfFileUpdateTasks(List<NodeDetails> nodes, ServerType processType) {
    String subGroupDescription = String.format("AnsibleConfigureServers (%s) for: %s",
        SubTaskGroupType.UpdatingGFlags, taskParams().nodePrefix);
    SubTaskGroup taskGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      taskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.GFlags,
                                         UpgradeTaskSubType.None));
    }
    taskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    subTaskGroupQueue.add(taskGroup);
  }

  // This is used for rolling upgrade, which is done per node in the universe.
  private void createSingleNodeUpgradeTasks(NodeDetails node, ServerType processType) {
    NodeDetails.NodeState nodeState = taskParams().taskType == UpgradeTaskType.Software
        ? UpgradeSoftware : UpdateGFlags;
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    createSetNodeStateTask(node, nodeState).setSubTaskGroupType(subGroupType);
    if (taskParams().taskType == UpgradeTaskType.Software) {
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
      SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServers (Software) for: " +
                                                   node.nodeName, executor);
      subTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software,
                                            UpgradeTaskSubType.Install));
      subTaskGroup.setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      subTaskGroupQueue.add(subTaskGroup);
    } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
      SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServers (GFlags) for :" +
                                                   node.nodeName, executor);
      subTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.GFlags,
                                            UpgradeTaskSubType.None));
      subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      subTaskGroupQueue.add(subTaskGroup);

      // Stop is done after conf file update to reduce unavailability.
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
    }

    createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
    createWaitForServersTasks(new HashSet<NodeDetails>(Arrays.asList(node)), processType);
    createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
        .setSubTaskGroupType(subGroupType);
    createWaitForKeyInMemoryTask(node);
    createSetNodeStateTask(node, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  // This is used for non-rolling upgrade, where each operation is done in parallel across all
  // the provided nodes per given process type.
  private void createMultipleNodeUpgradeTasks(List<NodeDetails> nodes, ServerType processType) {
    NodeDetails.NodeState nodeState = taskParams().taskType == UpgradeTaskType.Software ?
        UpgradeSoftware : UpdateGFlags;
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);
    createServerControlTasks(nodes, processType, "stop").setSubTaskGroupType(subGroupType);

    if (taskParams().taskType == UpgradeTaskType.Software) {
      String subGroupDescription = String.format("AnsibleConfigureServers (%s) for: %s",
          SubTaskGroupType.InstallingSoftware, taskParams().nodePrefix);
      SubTaskGroup installTaskGroup =  new SubTaskGroup(subGroupDescription, executor);
      for (NodeDetails node : nodes) {
        installTaskGroup.addTask(getConfigureTask(node, processType, UpgradeTaskType.Software,
                                                  UpgradeTaskSubType.Install));
      }
      installTaskGroup.setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      subTaskGroupQueue.add(installTaskGroup);
    }

    createServerControlTasks(nodes, processType, "start").setSubTaskGroupType(subGroupType);
    createSetNodeStateTasks(nodes, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
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
    // Add testing flag.
    params.itestS3PackagePath = taskParams().itestS3PackagePath;
    // Add task type
    params.type = type;
    params.setProperty("processType", processType.toString());
    params.setProperty("taskSubType", taskSubType.toString());

    if (type == UpgradeTaskType.Software) {
      params.ybSoftwareVersion = taskParams().ybSoftwareVersion;
    } else if (type == UpgradeTaskType.GFlags) {
      if (processType.equals(ServerType.MASTER)) {
        params.gflags = taskParams().masterGFlags;
        params.gflagsToRemove = userIntent.masterGFlags.keySet().stream().filter(
                flag -> !taskParams().masterGFlags.containsKey(flag)).collect(Collectors.toSet());
      } else {
        params.gflags = taskParams().tserverGFlags;
        params.gflagsToRemove = userIntent.tserverGFlags.keySet().stream().filter(
                flag -> !taskParams().tserverGFlags.containsKey(flag)).collect(Collectors.toSet());
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
}
