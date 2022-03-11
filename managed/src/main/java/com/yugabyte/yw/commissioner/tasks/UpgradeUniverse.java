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

import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Stopping;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateCert;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateGFlags;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpgradeSoftware;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert.UpdateRootCertAction;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.forms.UpgradeParams.UpgradeOption;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.CertificateInfo.Type;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class UpgradeUniverse extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpgradeUniverse.class);
  // Variable to mark if the loadbalancer state was changed.
  boolean loadbalancerOff = false;

  // Upgrade Task Type
  public enum UpgradeTaskType {
    Everything,
    Software,
    GFlags,
    Restart,
    Certs
  }

  public enum UpgradeTaskSubType {
    None,
    Download,
    Install,
    AppendNewRootCert,
    RotateCerts,
    RemoveOldRootCert
  }

  public static class Params extends UpgradeParams {}

  @Override
  protected UpgradeParams taskParams() {
    return (UpgradeParams) taskParams;
  }

  private void verifyParams(Universe universe, UserIntent primIntent) {
    switch (taskParams().taskType) {
      case Software:
        if (taskParams().upgradeOption == UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE) {
          throw new IllegalArgumentException("Software upgrade cannot be non restart.");
        }
        if (taskParams().ybSoftwareVersion == null || taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException(
              "Invalid yugabyte software version: " + taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(primIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException(
              "Software version is already: " + taskParams().ybSoftwareVersion);
        }
        break;
      case Restart:
        if (taskParams().upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
          throw new IllegalArgumentException(
              "Rolling restart operation of a universe needs to be of type rolling upgrade.");
        }
        break;
        // TODO: we need to fix this, right now if the gflags is empty on both master and tserver
        // we don't update the nodes properly but we do wipe the data from the backend (postgres).
        // JIRA ENG-2519 would track this.
      case GFlags:
        if (taskParams().masterGFlags.equals(primIntent.masterGFlags)
            && taskParams().tserverGFlags.equals(primIntent.tserverGFlags)) {
          throw new IllegalArgumentException("No gflags to change.");
        }
        break;
      case Certs:
        if (taskParams().upgradeOption == UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE) {
          throw new IllegalArgumentException("Cert update cannot be non restart.");
        }
        if (taskParams().certUUID == null) {
          throw new IllegalArgumentException("CertUUID cannot be null.");
        }
        CertificateInfo cert = CertificateInfo.get(taskParams().certUUID);
        if (cert == null) {
          throw new IllegalArgumentException("Certificate not present: " + taskParams().certUUID);
        }
        if (universe.getUniverseDetails().rootCA.equals(taskParams().certUUID)) {
          throw new IllegalArgumentException("Cluster already has the same cert.");
        }
        if (!taskParams().rotateRoot
            && CertificateHelper.areCertsDiff(
                universe.getUniverseDetails().rootCA, taskParams().certUUID)) {
          throw new IllegalArgumentException(
              "CA certificates cannot be different when rotateRoot set to false.");
        }
        if (taskParams().rotateRoot
            && !CertificateHelper.areCertsDiff(
                universe.getUniverseDetails().rootCA, taskParams().certUUID)) {
          throw new IllegalArgumentException("CA certificates are same. No cert rotation needed.");
        }
        if (cert.certType == Type.CustomCertHostPath
            && CertificateHelper.arePathsSame(
                universe.getUniverseDetails().rootCA, taskParams().certUUID)) {
          throw new IllegalArgumentException("The node cert/key paths cannot be same.");
        }
    }
  }

  private ImmutablePair<List<NodeDetails>, List<NodeDetails>> nodesToUpgrade(
      Universe universe, UserIntent intent) {
    List<NodeDetails> tServerNodes = new ArrayList<>();
    List<NodeDetails> masterNodes = new ArrayList<>();
    // Check the nodes that need to be upgraded.
    if (taskParams().taskType != UpgradeTaskType.GFlags) {
      tServerNodes = universe.getTServers();
      masterNodes = universe.getMasters();
    } else {
      // Master flags need to be changed.
      if (!taskParams().masterGFlags.equals(intent.masterGFlags)) {
        masterNodes = universe.getMasters();
      }
      // Tserver flags need to be changed.
      if (!taskParams().tserverGFlags.equals(intent.tserverGFlags)) {
        tServerNodes = universe.getTServers();
      }
    }
    // Retrieve master leader address of given universe
    final String leaderMasterAddress = universe.getMasterLeaderHostText();
    if (taskParams().upgradeOption == UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
      sortInRestartOrder(leaderMasterAddress, masterNodes);
    }
    return new ImmutablePair<>(masterNodes, tServerNodes);
  }

  @Override
  public void run() {
    try {
      checkUniverseVersion();
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
      UserIntent primIntent = primaryCluster.userIntent;

      // Check if the combination of taskType and upgradeOption are compatible.
      verifyParams(universe, primIntent);

      // Get the nodes that need to be upgraded.
      // Left element is master and right element is tserver.
      ImmutablePair<List<NodeDetails>, List<NodeDetails>> nodes =
          nodesToUpgrade(universe, primIntent);

      // Create all the necessary subtasks required for the required taskType and upgradeOption
      // combination.
      createServerUpgradeTasks(primaryCluster.uuid, nodes.getLeft(), nodes.getRight());

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
      if (loadbalancerOff) {
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

  // Find the master leader and move it to the end of the list.
  private void sortInRestartOrder(String leaderMasterAddress, List<NodeDetails> masterNodes) {
    boolean foundLeader = false;
    int numMasters = masterNodes.size();
    if (numMasters == 0) {
      return;
    }
    int masterLeaderIdx =
        IntStream.range(0, numMasters)
            .filter(i -> masterNodes.get(i).cloudInfo.private_ip.equals(leaderMasterAddress))
            .findFirst() // first occurrence
            .orElse(-1); // No element found
    if (masterLeaderIdx == -1) {
      throw new IllegalStateException(
          String.format("Master leader %s node not present in master list.", leaderMasterAddress));
    }
    // Move the master to the end of the list so that it updates last.
    Collections.swap(masterNodes, masterLeaderIdx, numMasters - 1);
  }

  private void createServerUpgradeTasks(
      UUID primaryClusterUuid, List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    if (taskParams().taskType != UpgradeTaskType.Certs) {
      // Setup subtasks for the taskTypes.
      if (taskParams().taskType == UpgradeTaskType.Software) {
        // TODO: This is assuming that master nodes is a subset of tserver node,
        // instead we should do a union.
        createDownloadTasks(tServerNodes);
      }

      // Upgrading inactive masters from the Primary cluster only.
      List<NodeDetails> inactiveMasterNodes =
          tServerNodes != null
              ? tServerNodes
                  .stream()
                  .filter(node -> node.placementUuid.equals(primaryClusterUuid))
                  .filter(node -> masterNodes == null || !masterNodes.contains(node))
                  .collect(Collectors.toList())
              : null;

      // Common subtasks.
      // 1. Upgrade active masters.
      if (masterNodes != null && !masterNodes.isEmpty()) {
        createAllUpgradeTasks(masterNodes, ServerType.MASTER, true);
      }

      // 2. Upgrade inactive masters.
      if ((taskParams().taskType == UpgradeTaskType.Software)
          && (inactiveMasterNodes != null)
          && !inactiveMasterNodes.isEmpty()) {
        createAllUpgradeTasks(inactiveMasterNodes, ServerType.MASTER, false);
      }

      // 3. Upgrade tservers.
      if (tServerNodes != null && !tServerNodes.isEmpty()) {
        createAllUpgradeTasks(tServerNodes, ServerType.TSERVER, true);
      }

      // Metadata updation subtasks.
      if (taskParams().taskType == UpgradeTaskType.Software) {
        // Update the software version on success.
        createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
            .setSubTaskGroupType(getTaskSubGroupType());
      } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
        // Update the list of parameter key/values in the universe with the new ones.
        updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
            .setSubTaskGroupType(getTaskSubGroupType());
      }
    } else {
      if (taskParams().rotateRoot && taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
        // Update the rootCA in platform to have both old cert and new cert
        createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
        // Append new root cert to the existing ca.crt
        createCertUpdateTasks(tServerNodes, UpgradeTaskSubType.AppendNewRootCert);
        // Do a rolling restart
        createRestartTasksForCertUpdate(masterNodes, tServerNodes);
        // Copy new server certs to all nodes
        createCertUpdateTasks(tServerNodes, UpgradeTaskSubType.RotateCerts);
        // Do a rolling restart
        createRestartTasksForCertUpdate(masterNodes, tServerNodes);
        // Remove old root cert from the ca.crt
        createCertUpdateTasks(tServerNodes, UpgradeTaskSubType.RemoveOldRootCert);
        // Reset the old rootCA content in platform
        createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
        // Update universe details with new cert values
        createUnivSetCertTask(taskParams().certUUID).setSubTaskGroupType(getTaskSubGroupType());
        // Do a rolling restart
        createRestartTasksForCertUpdate(masterNodes, tServerNodes);
      } else {
        // Update the rootCA in platform to have both old cert and new cert
        if (taskParams().rotateRoot) {
          createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
        }
        // Update server certs
        createCertUpdateTasks(tServerNodes, UpgradeTaskSubType.RotateCerts);
        // Restart master and tservers
        createRestartTasksForCertUpdate(masterNodes, tServerNodes);
        // Reset the old rootCA content in platform
        if (taskParams().rotateRoot) {
          createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
        }
        // Update rootCA param in universe details
        createUnivSetCertTask(taskParams().certUUID).setSubTaskGroupType(getTaskSubGroupType());
      }
    }
  }

  private void createAllUpgradeTasks(
      List<NodeDetails> nodes, ServerType processType, boolean isActiveProcess) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        // For a rolling upgrade, we need the data to not move, so
        // we disable the data load balancing.
        if (processType == ServerType.TSERVER) {
          createLoadBalancerStateChangeTask(false /*enable*/)
              .setSubTaskGroupType(getTaskSubGroupType());
          loadbalancerOff = true;
        }
        for (NodeDetails node : nodes) {
          createSingleNodeUpgradeTasks(node, processType, isActiveProcess);
        }
        if (loadbalancerOff) {
          createLoadBalancerStateChangeTask(true /*enable*/)
              .setSubTaskGroupType(getTaskSubGroupType());
          loadbalancerOff = false;
        }
        break;
      case NON_ROLLING_UPGRADE:
        createMultipleNonRollingNodeUpgradeTasks(nodes, processType, isActiveProcess);
        if (isActiveProcess) {
          createWaitForServersTasks(nodes, processType)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        }
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTasks(nodes, processType);
    }
  }

  // This is used for rolling upgrade, which is done per node in the universe.
  private void createSingleNodeUpgradeTasks(
      NodeDetails node, ServerType processType, boolean isActiveProcess) {
    NodeDetails.NodeState nodeState = null;
    switch (taskParams().taskType) {
      case Software:
        nodeState = UpgradeSoftware;
        break;
      case GFlags:
        nodeState = UpdateGFlags;
        break;
      case Restart:
        nodeState = Stopping;
        break;
      case Certs:
        nodeState = UpdateCert;
        break;
    }
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    createSetNodeStateTask(node, nodeState).setSubTaskGroupType(subGroupType);
    if (taskParams().taskType == UpgradeTaskType.Software) {
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
      createSoftwareInstallTasks(Arrays.asList(node), processType);
    } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
      createServerConfFileUpdateTasks(Arrays.asList(node), processType);
      // Stop is done after conf file update to reduce unavailability.
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
    }
    // For both rolling restart and a cert update, just a stop is good enough.
    else {
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
    }

    if (isActiveProcess) {
      createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
      createWaitForServersTasks(new HashSet<NodeDetails>(Arrays.asList(node)), processType);
      createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
          .setSubTaskGroupType(subGroupType);
      createWaitForKeyInMemoryTask(node);
    }
    createSetNodeStateTask(node, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  private void createNonRestartUpgradeTasks(List<NodeDetails> nodes, ServerType processType) {
    createServerConfFileUpdateTasks(nodes, processType);
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    createSetNodeStateTasks(nodes, UpdateGFlags).setSubTaskGroupType(subGroupType);

    createSetFlagInMemoryTasks(
        nodes,
        processType,
        true /* force */,
        processType == ServerType.MASTER ? taskParams().masterGFlags : taskParams().tserverGFlags,
        false /* updateMasterAddrs */);

    createSetNodeStateTasks(nodes, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  // This is used for non-rolling upgrade, where each operation is done in parallel across all
  // the provided nodes per given process type.
  private void createMultipleNonRollingNodeUpgradeTasks(
      List<NodeDetails> nodes, ServerType processType, boolean isActiveProcess) {
    if (taskParams().taskType == UpgradeTaskType.GFlags) {
      createServerConfFileUpdateTasks(nodes, processType);
    }
    NodeDetails.NodeState nodeState = null;
    switch (taskParams().taskType) {
      case Software:
        nodeState = UpgradeSoftware;
        break;
      case GFlags:
        nodeState = UpdateGFlags;
        break;
      case Certs:
        nodeState = UpdateCert;
        break;
    }
    SubTaskGroupType subGroupType = getTaskSubGroupType();
    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);
    createServerControlTasks(nodes, processType, "stop").setSubTaskGroupType(subGroupType);

    if (taskParams().taskType == UpgradeTaskType.Software) {
      createSoftwareInstallTasks(nodes, processType);
    }

    if (isActiveProcess) {
      createServerControlTasks(nodes, processType, "start").setSubTaskGroupType(subGroupType);
    }
    createSetNodeStateTasks(nodes, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  private void createRestartTasksForCertUpdate(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    if (taskParams().taskType == UpgradeTaskType.Certs) {
      if (masterNodes != null && !masterNodes.isEmpty()) {
        createAllUpgradeTasks(masterNodes, ServerType.MASTER, true);
      }
      if (tServerNodes != null && !tServerNodes.isEmpty()) {
        createAllUpgradeTasks(tServerNodes, ServerType.TSERVER, true);
      }
    } else {
      LOG.error("Restart cannot be performed as TaskType is not Certs: " + taskParams().taskType);
    }
  }

  private SubTaskGroupType getTaskSubGroupType() {
    switch (taskParams().taskType) {
      case Software:
        return SubTaskGroupType.UpgradingSoftware;
      case GFlags:
        return SubTaskGroupType.UpdatingGFlags;
      case Restart:
        return SubTaskGroupType.StoppingNodeProcesses;
      case Certs:
        return SubTaskGroupType.RotatingCert;
      default:
        return SubTaskGroupType.Invalid;
    }
  }

  private void createDownloadTasks(List<NodeDetails> nodes) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.DownloadingSoftware, taskParams().nodePrefix);
    SubTaskGroup downloadTaskGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      downloadTaskGroup.addTask(
          getConfigureTask(
              node, ServerType.TSERVER, UpgradeTaskType.Software, UpgradeTaskSubType.Download));
    }
    downloadTaskGroup.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    subTaskGroupQueue.add(downloadTaskGroup);
  }

  private void createCertUpdateTasks(List<NodeDetails> nodes, UpgradeTaskSubType subType) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.RotatingCert, taskParams().nodePrefix);
    SubTaskGroup rotateCertGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      rotateCertGroup.addTask(
          getConfigureTask(node, ServerType.TSERVER, UpgradeTaskType.Certs, subType));
    }
    rotateCertGroup.setSubTaskGroupType(SubTaskGroupType.RotatingCert);
    subTaskGroupQueue.add(rotateCertGroup);
  }

  private void createUniverseUpdateRootCertTask(UpdateRootCertAction updateAction) {
    SubTaskGroup taskGroup = new SubTaskGroup("UniverseUpdateRootCert", executor);
    UniverseUpdateRootCert.Params params = new UniverseUpdateRootCert.Params();
    params.universeUUID = taskParams().universeUUID;
    params.rootCA = taskParams().certUUID;
    params.action = updateAction;
    UniverseUpdateRootCert task = new UniverseUpdateRootCert();
    task.initialize(params);
    taskGroup.addTask(task);
    taskGroup.setSubTaskGroupType(getTaskSubGroupType());
    subTaskGroupQueue.add(taskGroup);
  }

  private void createServerConfFileUpdateTasks(List<NodeDetails> nodes, ServerType processType) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.UpdatingGFlags, taskParams().nodePrefix);
    SubTaskGroup taskGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      taskGroup.addTask(
          getConfigureTask(node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None));
    }
    taskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    subTaskGroupQueue.add(taskGroup);
  }

  private void createSoftwareInstallTasks(List<NodeDetails> nodes, ServerType processType) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.InstallingSoftware, taskParams().nodePrefix);
    SubTaskGroup taskGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      taskGroup.addTask(
          getConfigureTask(
              node, processType, UpgradeTaskType.Software, UpgradeTaskSubType.Install));
    }
    taskGroup.setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
    subTaskGroupQueue.add(taskGroup);
  }

  private int getSleepTimeForProcess(ServerType processType) {
    return processType == ServerType.MASTER
        ? taskParams().sleepAfterMasterRestartMillis
        : taskParams().sleepAfterTServerRestartMillis;
  }

  private AnsibleConfigureServers getConfigureTask(
      NodeDetails node,
      ServerType processType,
      UpgradeTaskType type,
      UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    UserIntent userIntent =
        Universe.getOrBadRequest(taskParams().universeUUID)
            .getUniverseDetails()
            .getClusterByUuid(node.placementUuid)
            .userIntent;
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
        params.gflagsToRemove =
            userIntent
                .masterGFlags
                .keySet()
                .stream()
                .filter(flag -> !taskParams().masterGFlags.containsKey(flag))
                .collect(Collectors.toSet());
      } else {
        params.gflags = taskParams().tserverGFlags;
        params.gflagsToRemove =
            userIntent
                .tserverGFlags
                .keySet()
                .stream()
                .filter(flag -> !taskParams().tserverGFlags.containsKey(flag))
                .collect(Collectors.toSet());
      }
    } else if (type == UpgradeTaskType.Certs) {
      params.rootCA = taskParams().certUUID;
      params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    }

    if (userIntent.providerType.equals(Common.CloudType.onprem)) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    // Create the Ansible task to get the server info.
    AnsibleConfigureServers task = new AnsibleConfigureServers();
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);

    return task;
  }
}
