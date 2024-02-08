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
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.ToggleTls;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateCert;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateGFlags;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpgradeSoftware;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateNodeDetails;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;

/**
 * @deprecated Use separate tasks based on UpgradeTaskBase
 */
@Deprecated
@Slf4j
public class UpgradeUniverse extends UniverseDefinitionTaskBase {
  // Variable to mark if the loadbalancer state was changed.
  boolean loadbalancerOff = false;

  @Inject
  protected UpgradeUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  private enum UpgradeIteration {
    Round1,
    Round2
  }

  public static class Params extends UpgradeParams {}

  private Map<UUID, List<String>> replacementRootVolumes = new ConcurrentHashMap<>();
  private Map<UUID, String> replacementRootDevices = new ConcurrentHashMap<>();
  private Map<UUID, UUID> nodeToRegion = new HashMap<>();

  @Override
  protected UpgradeParams taskParams() {
    return (UpgradeParams) taskParams;
  }

  private void verifyParams(Universe universe, UserIntent primIntent) {
    switch (taskParams().taskType) {
      case ResizeNode:
        if (taskParams().upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
          throw new IllegalArgumentException(
              "Only ROLLING_UPGRADE option is supported for resizing node (changing VM type).");
        }

        // Disk Size
        DeviceInfo deviceInfo = taskParams().getPrimaryCluster().userIntent.deviceInfo;
        if (deviceInfo != null) {
          // Disk size cannot decrease
          Integer currDiskSize = primIntent.deviceInfo.volumeSize;
          if (deviceInfo.volumeSize == null) {
            throw new IllegalArgumentException(
                "deviceInfo must contain volumeSize, or if you do not intend to change"
                    + "volume size, remove DeviceInfo");
          }
          if (currDiskSize > deviceInfo.volumeSize) {
            throw new IllegalArgumentException(
                "Disk size cannot decrease. It was "
                    + currDiskSize
                    + " got "
                    + deviceInfo.volumeSize);
          }

          // ResizeNode cannot change the number of volumes
          if (deviceInfo.numVolumes != null
              && !deviceInfo.numVolumes.equals(primIntent.deviceInfo.numVolumes)) {
            throw new IllegalArgumentException(
                "ResizeNode cannot change the number of volumes. It was "
                    + primIntent.deviceInfo.numVolumes
                    + " got "
                    + deviceInfo.numVolumes);
          }
        }

        // Instance Type
        // Make sure the instance type exists.
        String newInstanceTypeCode =
            taskParams().getPrimaryCluster().userIntent.getBaseInstanceType();
        String provider = primIntent.provider;

        List<InstanceType> instanceTypes =
            InstanceType.findByProvider(
                Provider.getOrBadRequest(UUID.fromString(provider)), confGetter);
        log.info(instanceTypes.toString());
        InstanceType newInstanceType =
            instanceTypes.stream()
                .filter(type -> type.getInstanceTypeCode().equals(newInstanceTypeCode))
                .findFirst()
                .orElse(null);
        if (newInstanceType == null) {
          throw new IllegalArgumentException(
              "Provider "
                  + primIntent.providerType
                  + " does not have the intended instance type "
                  + newInstanceTypeCode);
        }

        // Make sure instance type has the right storage
        if (newInstanceType.getInstanceTypeDetails() != null
            && newInstanceType.getInstanceTypeDetails().volumeDetailsList != null
            && newInstanceType.getInstanceTypeDetails().volumeDetailsList.size() > 0
            && newInstanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeType
                == InstanceType.VolumeType.NVME) {
          throw new IllegalArgumentException(
              "Instance type "
                  + newInstanceTypeCode
                  + " has NVME storage and is not supported by the ResizeNode operation");
        }
        if (primIntent.providerType.equals(Common.CloudType.aws)) {
          if (newInstanceTypeCode.contains("i3")) {
            throw new IllegalArgumentException(
                "ResizeNode operation does not support the instance type " + newInstanceTypeCode);
          }
          int dot_position = newInstanceTypeCode.indexOf('.');
          if (dot_position > 0 && newInstanceTypeCode.charAt(dot_position - 1) == 'd') {
            throw new IllegalArgumentException(
                "ResizeNode operation does not support the instance type " + newInstanceTypeCode);
          }
        }
        break;
      case VMImage:
        if (taskParams().upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
          throw new IllegalArgumentException(
              "Only ROLLING_UPGRADE option is supported for OS upgrades.");
        }

        for (NodeDetails node : universe.getUniverseDetails().nodeDetailsSet) {
          if (node.isMaster || node.isTserver) {
            Region region =
                AvailabilityZone.maybeGet(node.azUuid)
                    .map(az -> az.getRegion())
                    .orElseThrow(
                        () ->
                            new IllegalArgumentException(
                                "Could not find region for AZ " + node.cloudInfo.az));

            if (!taskParams().machineImages.containsKey(region.getUuid())) {
              throw new IllegalArgumentException(
                  "No VM image was specified for region " + node.cloudInfo.region);
            }

            nodeToRegion.putIfAbsent(node.nodeUuid, region.getUuid());
          }
        }
        break;
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
      case Systemd:
        if (taskParams().upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
          throw new IllegalArgumentException(
              "Systemd upgrade operation of a universe needs to be of type rolling upgrade.");
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
        if (taskParams().certUUID == null) {
          throw new IllegalArgumentException("CertUUID cannot be null");
        }
        CertificateInfo cert = CertificateInfo.get(taskParams().certUUID);
        if (cert == null) {
          throw new IllegalArgumentException("Certifcate not present: " + taskParams().certUUID);
        }
        if (universe.getUniverseDetails().rootCA.equals(taskParams().certUUID)) {
          throw new IllegalArgumentException("Cluster already has the same cert.");
        }
        if (!taskParams().rotateRoot
            && CertificateHelper.areCertsDiff(
                universe.getUniverseDetails().rootCA, taskParams().certUUID)) {
          throw new IllegalArgumentException("CA certificates cannot be different.");
        }
        if (CertificateHelper.arePathsSame(
            universe.getUniverseDetails().rootCA, taskParams().certUUID)) {
          throw new IllegalArgumentException("The node cert/key paths cannot be same.");
        }
        if (taskParams().upgradeOption == UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE) {
          throw new IllegalArgumentException("Cert update cannot be non restart.");
        }
        break;
      case ToggleTls:
        if (taskParams().upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE
            && taskParams().upgradeOption != UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE) {
          throw new IllegalArgumentException(
              "Toggle TLS operation needs to be of type either rolling or non-rolling upgrade");
        }
        if (taskParams().enableNodeToNodeEncrypt == primIntent.enableNodeToNodeEncrypt
            && taskParams().enableClientToNodeEncrypt == primIntent.enableClientToNodeEncrypt) {
          throw new IllegalArgumentException(
              "No change in node-to-node or client-to-node properties");
        }
        if ((taskParams().enableNodeToNodeEncrypt || taskParams().enableClientToNodeEncrypt)
            && taskParams().rootCA == null) {
          throw new IllegalArgumentException("Root certificate cannot be null when enabling TLS");
        }
        break;
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
      masterNodes = sortMastersInRestartOrder(leaderMasterAddress, masterNodes);
      tServerNodes = sortTServersInRestartOrder(universe, tServerNodes);
    }
    return new ImmutablePair<>(masterNodes, tServerNodes);
  }

  @Override
  public void run() {
    try {
      checkUniverseVersion();
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
      UserIntent primIntent = primaryCluster.userIntent;

      // Check if the combination of taskType and upgradeOption are compatible.
      verifyParams(universe, primIntent);
      taskParams().ybPrevSoftwareVersion = primIntent.ybSoftwareVersion;

      preTaskActions();

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
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error={}.", getName(), t);
      // If the task failed, we don't want the loadbalancer to be disabled,
      // so we enable it again in case of errors.
      if (loadbalancerOff) {
        setTaskQueueAndRun(
            () -> {
              createLoadBalancerStateChangeTask(true /*enable*/)
                  .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
            });
      }

      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }

  // Find the master leader and move it to the end of the list.
  private List<NodeDetails> sortMastersInRestartOrder(
      String leaderMasterAddress, List<NodeDetails> nodes) {
    if (nodes.isEmpty()) {
      return nodes;
    }
    return nodes.stream()
        .sorted(
            Comparator.<NodeDetails, Boolean>comparing(
                    node -> leaderMasterAddress.equals(node.cloudInfo.private_ip))
                .thenComparing(NodeDetails::getNodeIdx))
        .collect(Collectors.toList());
  }

  // Find the master leader and move it to the end of the list.
  private List<NodeDetails> sortTServersInRestartOrder(Universe universe, List<NodeDetails> nodes) {
    if (nodes.isEmpty()) {
      return nodes;
    }

    Map<UUID, Map<UUID, PlacementInfo.PlacementAZ>> placementAZMapPerCluster =
        PlacementInfoUtil.getPlacementAZMapPerCluster(universe);
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    return nodes.stream()
        .sorted(
            Comparator.<NodeDetails, Boolean>comparing(
                    // Fully upgrade primary cluster first
                    node -> !node.placementUuid.equals(primaryClusterUuid))
                .thenComparing(
                    node -> {
                      Map<UUID, PlacementInfo.PlacementAZ> placementAZMap =
                          placementAZMapPerCluster.get(node.placementUuid);
                      if (placementAZMap == null) {
                        // Well, this shouldn't happen - but just to make sure we'll not fail - sort
                        // to the end
                        return true;
                      }
                      PlacementInfo.PlacementAZ placementAZ = placementAZMap.get(node.azUuid);
                      if (placementAZ == null) {
                        return true;
                      }
                      // Primary zones go first
                      return !placementAZ.isAffinitized;
                    })
                .thenComparing(NodeDetails::getNodeIdx))
        .collect(Collectors.toList());
  }

  private SubTaskGroup createChangeInstanceTypeTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ChangeInstanceType");
    ChangeInstanceType.Params params = new ChangeInstanceType.Params();

    params.nodeName = node.nodeName;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.azUuid = node.azUuid;
    params.instanceType = taskParams().getPrimaryCluster().userIntent.getInstanceTypeForNode(node);

    ChangeInstanceType changeInstanceTypeTask = createTask(ChangeInstanceType.class);
    changeInstanceTypeTask.initialize(params);
    subTaskGroup.addSubTask(changeInstanceTypeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createRootVolumeReplacementTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ReplaceRootVolume");
    ReplaceRootVolume.Params replaceParams = new ReplaceRootVolume.Params();
    replaceParams.nodeName = node.nodeName;
    replaceParams.azUuid = node.azUuid;
    replaceParams.setUniverseUUID(taskParams().getUniverseUUID());
    replaceParams.bootDisksPerZone = this.replacementRootVolumes;
    replaceParams.rootDevicePerZone = this.replacementRootDevices;

    ReplaceRootVolume replaceDiskTask = createTask(ReplaceRootVolume.class);
    replaceDiskTask.initialize(replaceParams);
    subTaskGroup.addSubTask(replaceDiskTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createRootVolumeCreationTasks(List<NodeDetails> nodes) {
    Map<UUID, List<NodeDetails>> rootVolumesPerAZ =
        nodes.stream().collect(Collectors.groupingBy(n -> n.azUuid));
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateRootVolumes");
    rootVolumesPerAZ
        .entrySet()
        .forEach(
            e -> {
              NodeDetails node = e.getValue().get(0);
              UUID region = this.nodeToRegion.get(node.nodeUuid);
              String machineImage = taskParams().machineImages.get(region);
              int numVolumes = e.getValue().size();

              if (!taskParams().forceVMImageUpgrade) {
                numVolumes =
                    (int)
                        e.getValue().stream()
                            .filter(n -> !machineImage.equals(n.machineImage))
                            .count();
              }

              if (numVolumes == 0) {
                log.info("Nothing to upgrade in AZ {}", node.cloudInfo.az);
                return;
              }

              CreateRootVolumes.Params params = new CreateRootVolumes.Params();
              UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
              fillCreateParamsForNode(params, userIntent, node);
              params.numVolumes = numVolumes;
              params.setMachineImage(machineImage);
              params.bootDisksPerZone = replacementRootVolumes;
              params.rootDevicePerZone = replacementRootDevices;

              log.info(
                  "Creating {} root volumes using {} in AZ {}",
                  params.numVolumes,
                  params.getMachineImage(),
                  node.cloudInfo.az);

              CreateRootVolumes task = createTask(CreateRootVolumes.class);
              task.initialize(params);
              subTaskGroup.addSubTask(task);
            });
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createNodeDetailsUpdateTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateNodeDetails");
    UpdateNodeDetails.Params updateNodeDetailsParams = new UpdateNodeDetails.Params();
    updateNodeDetailsParams.setUniverseUUID(taskParams().getUniverseUUID());
    updateNodeDetailsParams.azUuid = node.azUuid;
    updateNodeDetailsParams.nodeName = node.nodeName;
    updateNodeDetailsParams.details = node;

    UpdateNodeDetails updateNodeTask = createTask(UpdateNodeDetails.class);
    updateNodeTask.initialize(updateNodeDetailsParams);
    updateNodeTask.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(updateNodeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private void createServerUpgradeTasks(
      UUID primaryClusterUuid, List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    createPreUpgradeTasks(masterNodes, tServerNodes);
    createUpgradeTasks(primaryClusterUuid, masterNodes, tServerNodes, UpgradeIteration.Round1);
    createMetadataUpdateTasks();
    createUpgradeTasks(primaryClusterUuid, masterNodes, tServerNodes, UpgradeIteration.Round2);
    createPostUpgradeTasks();
  }

  private void createPreUpgradeTasks(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    if (taskParams().taskType == UpgradeTaskType.Software) {
      // TODO: This is assuming that master nodes is a subset of tserver node,
      // instead we should do a union.
      createDownloadTasks(tServerNodes);
    } else if (taskParams().taskType == UpgradeTaskType.Certs) {
      createCertUpdateTasks(tServerNodes);
    } else if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
      createCopyCertTasks(tServerNodes);
    }
  }

  private void createResizeNodeTasks(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    Set<NodeDetails> nodes = new LinkedHashSet<>();
    nodes.addAll(masterNodes);
    nodes.addAll(tServerNodes);

    UserIntent currUserIntent =
        Universe.getOrBadRequest(taskParams().getUniverseUUID())
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent;

    Integer currDiskSize = currUserIntent.deviceInfo.volumeSize;

    String currInstanceType = currUserIntent.instanceType;

    // Todo: Add preflight checks here

    // Change disk size
    DeviceInfo deviceInfo = taskParams().getPrimaryCluster().userIntent.deviceInfo;
    if (deviceInfo != null) {
      Integer newDiskSize = deviceInfo.volumeSize;
      // Check if the storage needs to be resized
      if (taskParams().forceResizeNode || !currDiskSize.equals(newDiskSize)) {
        log.info("Resizing disk from {} to {}", currDiskSize, newDiskSize);

        // Resize the nodes' disks
        createUpdateDiskSizeTasks(nodes).setSubTaskGroupType(SubTaskGroupType.ResizingDisk);

        // Persist changes in the universe
        createPersistResizeNodeTask(
                taskParams().getPrimaryCluster().userIntent, taskParams().getPrimaryCluster().uuid)
            .setSubTaskGroupType(SubTaskGroupType.ResizingDisk);
      } else {
        log.info(
            "Skipping resizing disk as both old and new sizes are {}, "
                + "and forceResizeNode flag is false",
            currDiskSize);
      }
    }

    // Change instance type
    String newInstanceType = taskParams().getPrimaryCluster().userIntent.instanceType;
    if (taskParams().forceResizeNode || !currInstanceType.equals(newInstanceType)) {
      for (NodeDetails node : nodes) {
        // Check if the node needs to be resized
        if (!taskParams().forceResizeNode && node.cloudInfo.instance_type.equals(newInstanceType)) {
          log.info("Skipping node {} as its type is already {}", node.nodeName, currInstanceType);
          continue;
        }

        // Update node state to Resizing
        createSetNodeStateTask(node, NodeDetails.NodeState.Resizing)
            .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);

        // Stop the tserver.
        if (node.isTserver) {
          createTServerTaskForNode(node, "stop")
              .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);
        }

        // Stop the master process on this node.
        if (node.isMaster) {
          createStopMasterTasks(new HashSet<NodeDetails>(Arrays.asList(node)))
              .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);

          // If RF is 1, we can just move forward, since there is no other master.
          if (currUserIntent.replicationFactor != 1) {
            createWaitForMasterLeaderTask()
                .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);
            createChangeConfigTasks(node, false /* isAdd */, SubTaskGroupType.ChangeInstanceType);
          }
        }

        // Change the instance type
        createChangeInstanceTypeTask(node).setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);

        // Persist the new instance type in the node details
        node.cloudInfo.instance_type = newInstanceType;
        createNodeDetailsUpdateTask(node).setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);

        // Start the master process on this node.
        if (node.isMaster) {
          // Set gflags for master.
          createGFlagsOverrideTasks(ImmutableList.of(node), ServerType.MASTER);

          // Start a master process.
          createStartMasterTasks(new HashSet<NodeDetails>(Arrays.asList(node)))
              .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);

          // Wait for the master to be responsive.
          createWaitForServersTasks(
                  new HashSet<NodeDetails>(Arrays.asList(node)), ServerType.MASTER)
              .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);

          if (currUserIntent.replicationFactor != 1) {
            // Add stopped master to the quorum.
            createChangeConfigTasks(node, true /* isAdd */, SubTaskGroupType.ConfigureUniverse);
          }
          // If there are no universe keys on the universe, it will have no effect.
          if (EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
            createSetActiveUniverseKeysTask()
                .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          }
        }

        // Start the tserver process on this node.
        if (node.isTserver) {
          // Start the tserver process
          createTServerTaskForNode(node, "start")
              .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);

          // Wait for the tablet server to be responsive.
          createWaitForServersTasks(
                  new HashSet<NodeDetails>(Arrays.asList(node)), ServerType.TSERVER)
              .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);
        }

        // Update node state to Live
        createSetNodeStateTask(node, NodeDetails.NodeState.Live)
            .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);
      }

      // Persist changes in the universe
      createPersistResizeNodeTask(
              taskParams().getPrimaryCluster().userIntent, taskParams().getPrimaryCluster().uuid)
          .setSubTaskGroupType(SubTaskGroupType.ChangeInstanceType);
    }
  }

  // For systemd upgrades
  public void createSystemdUpgradeTasks(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    Set<NodeDetails> nodes = new LinkedHashSet<>();
    nodes.addAll(masterNodes);
    nodes.addAll(tServerNodes);
    SubTaskGroupType subGroupType = getTaskSubGroupType();

    for (NodeDetails node : nodes) {
      // Update node state to Stopping
      createSetNodeStateTask(node, NodeDetails.NodeState.Stopping)
          .setSubTaskGroupType(subGroupType);

      List<NodeDetails> nodeList = Collections.singletonList(node);
      ServerType processType = null;
      if (node.isMaster) {
        processType = ServerType.MASTER;
      } else {
        processType = ServerType.TSERVER;
      }

      // Stop yb-master and yb-tserver on node
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
      // Conditional Provisioning
      createSetupServerTasks(nodeList, p -> p.isSystemdUpgrade = true)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);
      // Conditional Configuring
      createConfigureServerTasks(nodeList, params -> params.isSystemdUpgrade = true)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      subGroupType = SubTaskGroupType.ConfigureUniverse;
      // Start using systemd services.
      createServerControlTask(node, processType, "start", params -> params.useSystemd = true)
          .setSubTaskGroupType(subGroupType);

      // Wait for server to get ready
      createWaitForServersTasks(nodeList, processType).setSubTaskGroupType(subGroupType);
      createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
          .setSubTaskGroupType(subGroupType);

      // If there are no universe keys on the universe, it will have no effect.
      if (processType == ServerType.MASTER
          && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
        createSetActiveUniverseKeysTask().setSubTaskGroupType(subGroupType);
      }
      createWaitForKeyInMemoryTask(node).setSubTaskGroupType(subGroupType);

      // Update node state to Live
      createSetNodeStateTask(node, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
    }
    // Persist systemd upgrade changes in the universe
    createPersistSystemdUpgradeTask(true /* useSystemd */).setSubTaskGroupType(subGroupType);
  }

  private void createUpgradeTasks(
      UUID primaryClusterUuid,
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeIteration upgradeIteration) {
    // Currently two round upgrade is needed only for ToggleTls
    if (upgradeIteration == UpgradeIteration.Round2
        && taskParams().taskType != UpgradeTaskType.ToggleTls) {
      return;
    }

    if (taskParams().taskType == UpgradeTaskType.ResizeNode) {
      createResizeNodeTasks(masterNodes, tServerNodes);
      return;
    }

    if (taskParams().taskType == UpgradeTaskType.Systemd) {
      createSystemdUpgradeTasks(masterNodes, tServerNodes);
      return;
    }

    if (taskParams().taskType != UpgradeTaskType.VMImage) {
      UpgradeParams.UpgradeOption upgradeOption = taskParams().upgradeOption;
      if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
        int nodeToNodeChange =
            getNodeToNodeChangeForToggleTls(
                Universe.getOrBadRequest(taskParams().getUniverseUUID())
                    .getUniverseDetails()
                    .getPrimaryCluster()
                    .userIntent,
                taskParams());
        if (nodeToNodeChange > 0) {
          // Setting allow_insecure to false can be done in non-restart way
          if (upgradeIteration == UpgradeIteration.Round2) {
            upgradeOption = UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE;
          }
        } else if (nodeToNodeChange < 0) {
          // Setting allow_insecure to true can be done in non-restart way
          if (upgradeIteration == UpgradeIteration.Round1) {
            upgradeOption = UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE;
          }
        } else {
          // Two round upgrade not needed when there is no change in node-to-node
          if (upgradeIteration == UpgradeIteration.Round2) {
            return;
          }
        }
      }

      // Upgrading inactive masters from the Primary cluster only.
      List<NodeDetails> inactiveMasterNodes =
          tServerNodes != null
              ? tServerNodes.stream()
                  .filter(node -> node.placementUuid.equals(primaryClusterUuid))
                  .filter(node -> masterNodes == null || !masterNodes.contains(node))
                  .collect(Collectors.toList())
              : null;

      // Common subtasks
      // 1. Upgrade active masters.
      if (masterNodes != null && !masterNodes.isEmpty()) {
        createAllUpgradeTasks(
            masterNodes, ServerType.MASTER, upgradeIteration, upgradeOption, true);
      }

      // 2. Upgrade inactive masters.
      if ((taskParams().taskType == UpgradeTaskType.Software)
          && (inactiveMasterNodes != null)
          && !inactiveMasterNodes.isEmpty()) {
        createAllUpgradeTasks(
            inactiveMasterNodes, ServerType.MASTER, upgradeIteration, upgradeOption, false);
      }

      // 3. Upgrade tservers.
      if (tServerNodes != null && !tServerNodes.isEmpty()) {
        createAllUpgradeTasks(
            tServerNodes, ServerType.TSERVER, upgradeIteration, upgradeOption, true);
      }
    } else {
      SubTaskGroupType subGroupType = getTaskSubGroupType();
      Set<NodeDetails> nodes = new LinkedHashSet<>();
      // FIXME: proper equals/hashCode for NodeDetails
      nodes.addAll(masterNodes);
      nodes.addAll(tServerNodes);

      createRootVolumeCreationTasks(new ArrayList<>(nodes)).setSubTaskGroupType(subGroupType);

      for (NodeDetails node : nodes) {
        UUID region = this.nodeToRegion.get(node.nodeUuid);
        String machineImage = taskParams().machineImages.get(region);

        if (!taskParams().forceVMImageUpgrade && machineImage.equals(node.machineImage)) {
          log.info(
              "Skipping node {} as it's already running on {} and force flag is not set",
              node.nodeName,
              machineImage);
          continue;
        }

        List<UniverseTaskBase.ServerType> processTypes = new ArrayList<>();
        if (node.isMaster) processTypes.add(ServerType.MASTER);
        if (node.isTserver) processTypes.add(ServerType.TSERVER);

        processTypes.forEach(
            processType ->
                createServerControlTask(node, processType, "stop")
                    .setSubTaskGroupType(subGroupType));
        createRootVolumeReplacementTask(node).setSubTaskGroupType(subGroupType);

        List<NodeDetails> nodeList = Collections.singletonList(node);

        createSetupServerTasks(nodeList).setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
        createConfigureServerTasks(nodeList, params -> {})
            .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

        processTypes.forEach(
            processType -> {
              createGFlagsOverrideTasks(nodeList, processType);
              createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
              createWaitForServersTasks(new HashSet<NodeDetails>(nodeList), processType);
              createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
                  .setSubTaskGroupType(subGroupType);
              // If there are no universe keys on the universe, it will have no effect.
              if (processType == ServerType.MASTER
                  && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
                createSetActiveUniverseKeysTask().setSubTaskGroupType(subGroupType);
              }
            });
        createWaitForKeyInMemoryTask(node);

        node.machineImage = machineImage;
        createNodeDetailsUpdateTask(node).setSubTaskGroupType(subGroupType);
      }
    }
  }

  private void createPostUpgradeTasks() {
    if (taskParams().taskType == UpgradeTaskType.Software) {
      if (taskParams().upgradeSystemCatalog) {
        // Run YSQL upgrade on the universe
        createRunYsqlUpgradeTask(taskParams().ybSoftwareVersion)
            .setSubTaskGroupType(getTaskSubGroupType());
      }
      // Update the software version on success.
      createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
          .setSubTaskGroupType(getTaskSubGroupType());
    } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
      // Update the list of parameter key/values in the universe with the new ones.
      updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
          .setSubTaskGroupType(getTaskSubGroupType());
    } else if (taskParams().taskType == UpgradeTaskType.Certs) {
      createUnivSetCertTask(taskParams().certUUID).setSubTaskGroupType(getTaskSubGroupType());
    }
  }

  private void createMetadataUpdateTasks() {
    if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
      createUniverseSetTlsParamsTask();
    }
  }

  private void createAllUpgradeTasks(
      List<NodeDetails> nodes,
      ServerType processType,
      UpgradeIteration upgradeIteration,
      UpgradeParams.UpgradeOption upgradeOption,
      boolean isActiveProcess) {
    switch (upgradeOption) {
      case ROLLING_UPGRADE:
        // For a rolling upgrade, we need the data to not move, so
        // we disable the data load balancing.
        if (processType == ServerType.TSERVER) {
          createLoadBalancerStateChangeTask(false /*enable*/)
              .setSubTaskGroupType(getTaskSubGroupType());
          loadbalancerOff = true;
        }
        for (NodeDetails node : nodes) {
          createSingleNodeUpgradeTasks(node, processType, upgradeIteration, isActiveProcess);
        }
        if (loadbalancerOff) {
          createLoadBalancerStateChangeTask(true /*enable*/)
              .setSubTaskGroupType(getTaskSubGroupType());
          loadbalancerOff = false;
        }
        break;
      case NON_ROLLING_UPGRADE:
        createMultipleNonRollingNodeUpgradeTasks(
            nodes, processType, upgradeIteration, isActiveProcess);
        if (isActiveProcess) {
          createWaitForServersTasks(nodes, processType)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          // If there are no universe keys on the universe, it will have no effect.
          if (processType == ServerType.MASTER
              && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
            createSetActiveUniverseKeysTask()
                .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          }
        }
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTasks(nodes, processType, upgradeIteration);
    }
  }

  // This is used for rolling upgrade, which is done per node in the universe.
  private void createSingleNodeUpgradeTasks(
      NodeDetails node,
      ServerType processType,
      UpgradeIteration upgradeIteration,
      boolean isActiveProcess) {
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
      case ToggleTls:
        nodeState = ToggleTls;
        break;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    createSetNodeStateTask(node, nodeState).setSubTaskGroupType(subGroupType);
    if (taskParams().taskType == UpgradeTaskType.Software) {
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
      createSoftwareInstallTasks(
          Collections.singletonList(node),
          processType,
          taskParams().ybSoftwareVersion,
          subGroupType);
    } else if (taskParams().taskType == UpgradeTaskType.GFlags) {
      createServerConfFileUpdateTasks(Collections.singletonList(node), processType);
      // Stop is done after conf file update to reduce unavailability.
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
    } else if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
      createToggleTlsTasks(Collections.singletonList(node), processType, upgradeIteration);
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
    }
    // For both rolling restart and a cert update, just a stop is good enough.
    else {
      createServerControlTask(node, processType, "stop").setSubTaskGroupType(subGroupType);
    }

    if (isActiveProcess) {
      createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
      createWaitForServersTasks(new HashSet<>(Collections.singletonList(node)), processType)
          .setSubTaskGroupType(subGroupType);
      createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
          .setSubTaskGroupType(subGroupType);
      if (processType == ServerType.MASTER
          && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
        // If there are no universe keys on the universe, it will have no effect.
        createSetActiveUniverseKeysTask().setSubTaskGroupType(subGroupType);
      }
      createWaitForKeyInMemoryTask(node).setSubTaskGroupType(subGroupType);
    }
    createSetNodeStateTask(node, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  private void createNonRestartUpgradeTasks(
      List<NodeDetails> nodes, ServerType processType, UpgradeIteration upgradeIteration) {
    NodeDetails.NodeState nodeState = null;
    SubTaskGroupType subGroupType = getTaskSubGroupType();

    if (taskParams().taskType == UpgradeTaskType.GFlags) {
      nodeState = UpdateGFlags;
      createServerConfFileUpdateTasks(nodes, processType);
    } else if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
      nodeState = ToggleTls;
      createToggleTlsTasks(nodes, processType, upgradeIteration);
    }

    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);

    if (taskParams().taskType == UpgradeTaskType.GFlags) {
      createSetFlagInMemoryTasks(
              nodes,
              processType,
              true,
              processType == ServerType.MASTER
                  ? taskParams().masterGFlags
                  : taskParams().tserverGFlags)
          .setSubTaskGroupType(subGroupType);
    } else if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
      Map<String, String> gflags = new HashMap<>();
      gflags.put(
          "allow_insecure_connections",
          upgradeIteration == UpgradeIteration.Round1 ? "true" : "false");
      createSetFlagInMemoryTasks(nodes, processType, true, gflags)
          .setSubTaskGroupType(subGroupType);
    }

    createSetNodeStateTasks(nodes, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  // This is used for non-rolling upgrade, where each operation is done in parallel across all
  // the provided nodes per given process type.
  private void createMultipleNonRollingNodeUpgradeTasks(
      List<NodeDetails> nodes,
      ServerType processType,
      UpgradeIteration upgradeIteration,
      boolean isActiveProcess) {
    if (taskParams().taskType == UpgradeTaskType.GFlags) {
      createServerConfFileUpdateTasks(nodes, processType);
    } else if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
      createToggleTlsTasks(nodes, processType, upgradeIteration);
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
      case ToggleTls:
        nodeState = ToggleTls;
        break;
    }

    SubTaskGroupType subGroupType = getTaskSubGroupType();
    createSetNodeStateTasks(nodes, nodeState).setSubTaskGroupType(subGroupType);
    createServerControlTasks(nodes, processType, "stop").setSubTaskGroupType(subGroupType);

    if (taskParams().taskType == UpgradeTaskType.Software) {
      createSoftwareInstallTasks(nodes, processType, taskParams().ybSoftwareVersion, subGroupType);
    }

    if (isActiveProcess) {
      createServerControlTasks(nodes, processType, "start").setSubTaskGroupType(subGroupType);
    }
    createSetNodeStateTasks(nodes, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  private SubTaskGroupType getTaskSubGroupType() {
    switch (taskParams().taskType) {
      case Software:
        return SubTaskGroupType.UpgradingSoftware;
      case GFlags:
        return SubTaskGroupType.UpdatingGFlags;
      case Restart:
        return SubTaskGroupType.StoppingNodeProcesses;
      case Systemd:
        return SubTaskGroupType.SystemdUpgrade;
      case ToggleTls:
        return SubTaskGroupType.ToggleTls;
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
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getConfigureTask(
              node, ServerType.TSERVER, UpgradeTaskType.Software, UpgradeTaskSubType.Download));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createCertUpdateTasks(List<NodeDetails> nodes) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.RotatingCert, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getConfigureTask(
              node, ServerType.TSERVER, UpgradeTaskType.Certs, UpgradeTaskSubType.None));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.RotatingCert);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createCopyCertTasks(List<NodeDetails> nodes) {
    // Copy cert tasks are not needed if TLS is disabled
    if (!taskParams().enableNodeToNodeEncrypt && !taskParams().enableClientToNodeEncrypt) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.ToggleTls, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getConfigureTask(
              node, ServerType.TSERVER, UpgradeTaskType.ToggleTls, UpgradeTaskSubType.CopyCerts));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ToggleTls);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
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
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getConfigureTask(node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createToggleTlsTasks(
      List<NodeDetails> nodes, ServerType processType, UpgradeIteration upgradeIteration) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.ToggleTls, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getConfigureTask(
              node,
              processType,
              UpgradeTaskType.ToggleTls,
              upgradeIteration == UpgradeIteration.Round1
                  ? UpgradeTaskSubType.Round1GFlagsUpdate
                  : UpgradeTaskSubType.Round2GFlagsUpdate));
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ToggleTls);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createUniverseSetTlsParamsTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UniverseSetTlsParams");
    UniverseSetTlsParams.Params params = new UniverseSetTlsParams.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.enableNodeToNodeEncrypt = taskParams().enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = taskParams().enableClientToNodeEncrypt;
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    params.allowInsecure = taskParams().allowInsecure;
    params.rootCA = taskParams().rootCA;
    params.clientRootCA = taskParams().getClientRootCA();

    UniverseSetTlsParams task = createTask(UniverseSetTlsParams.class);
    task.initialize(params);
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ToggleTls);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private int getNodeToNodeChangeForToggleTls(UserIntent userIntent, UpgradeParams params) {
    return userIntent.enableNodeToNodeEncrypt != params.enableNodeToNodeEncrypt
        ? (params.enableNodeToNodeEncrypt ? 1 : -1)
        : 0;
  }

  private AnsibleConfigureServers getConfigureTask(
      NodeDetails node,
      ServerType processType,
      UpgradeTaskType type,
      UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UserIntent userIntent =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent;
    // Set the device information (numVolumes, volumeSize, etc.)
    params.deviceInfo = userIntent.getDeviceInfoForNode(node);
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
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

    // Sets the isMaster field
    params.isMaster = node.isMaster;
    params.enableYSQL = userIntent.enableYSQL;
    params.enableYCQL = userIntent.enableYCQL;
    params.enableYCQLAuth = userIntent.enableYCQLAuth;
    params.enableYSQLAuth = userIntent.enableYSQLAuth;
    params.auditLogConfig = userIntent.auditLogConfig;

    // The software package to install for this cluster.
    params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
    params.setYbcSoftwareVersion(taskParams().getYbcSoftwareVersion());
    params.ybcGflags = userIntent.ybcFlags;
    // Set the InstanceType
    params.instanceType = node.cloudInfo.instance_type;
    params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    params.rootAndClientRootCASame = universe.getUniverseDetails().rootAndClientRootCASame;

    params.allowInsecure = universe.getUniverseDetails().allowInsecure;
    params.setTxnTableWaitCountFlag = universe.getUniverseDetails().setTxnTableWaitCountFlag;
    params.rootCA = universe.getUniverseDetails().rootCA;
    params.setClientRootCA(universe.getUniverseDetails().getClientRootCA());
    params.enableYEDIS = userIntent.enableYEDIS;
    params.useSystemd = userIntent.useSystemd;

    UUID custUUID = Customer.get(universe.getCustomerId()).getUuid();
    params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);

    if (type == UpgradeTaskType.Software) {
      params.ybSoftwareVersion = taskParams().ybSoftwareVersion;
    } else if (type == UpgradeTaskType.GFlags) {
      if (processType.equals(ServerType.MASTER)) {
        params.gflags = taskParams().masterGFlags;
        params.gflagsToRemove =
            GFlagsUtil.getDeletedGFlags(userIntent.masterGFlags, taskParams().masterGFlags);
      } else {
        params.gflags = taskParams().tserverGFlags;
        params.gflagsToRemove =
            GFlagsUtil.getDeletedGFlags(userIntent.tserverGFlags, taskParams().tserverGFlags);
      }
    } else if (type == UpgradeTaskType.Certs) {
      params.rootCA = taskParams().certUUID;
    } else if (type == UpgradeTaskType.ToggleTls) {
      params.enableNodeToNodeEncrypt = taskParams().enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = taskParams().enableClientToNodeEncrypt;
      params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
      params.allowInsecure = taskParams().allowInsecure;
      params.rootCA = taskParams().rootCA;
      params.setClientRootCA(taskParams().getClientRootCA());
      params.nodeToNodeChange = getNodeToNodeChangeForToggleTls(userIntent, taskParams());
    }

    if (userIntent.providerType.equals(Common.CloudType.onprem)) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    // Create the Ansible task to get the server info.
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());

    return task;
  }
}
