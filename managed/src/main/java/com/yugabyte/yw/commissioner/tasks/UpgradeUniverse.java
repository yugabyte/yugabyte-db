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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.*;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;

import javax.inject.Inject;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;

import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.*;

@Slf4j
public class UpgradeUniverse extends UniverseDefinitionTaskBase {
  // Variable to mark if the loadbalancer state was changed.
  boolean loadbalancerOff = false;

  @Inject
  protected UpgradeUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Upgrade Task Type
  public enum UpgradeTaskType {
    Everything,
    Software,
    VMImage,
    GFlags,
    Restart,
    Certs,
    ToggleTls
  }

  public enum UpgradeTaskSubType {
    None,
    Download,
    Install,
    CopyCerts,
    Round1GFlagsUpdate,
    Round2GFlagsUpdate
  }

  private enum UpgradeIteration {
    Round1,
    Round2
  }

  public static class Params extends UpgradeParams {}

  private Map<UUID, List<String>> replacementRootVolumes = new ConcurrentHashMap<>();
  private Map<UUID, UUID> nodeToRegion = new HashMap<>();

  @Override
  protected UpgradeParams taskParams() {
    return (UpgradeParams) taskParams;
  }

  private void verifyParams(Universe universe, UserIntent primIntent) {
    switch (taskParams().taskType) {
      case VMImage:
        if (taskParams().upgradeOption != UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
          throw new IllegalArgumentException(
              "Only ROLLING_UPGRADE option is supported for OS upgrades.");
        }

        for (NodeDetails node : universe.getUniverseDetails().nodeDetailsSet) {
          if (node.isMaster || node.isTserver) {
            Region region =
                AvailabilityZone.maybeGet(node.azUuid)
                    .map(az -> az.region)
                    .orElseThrow(
                        () ->
                            new IllegalArgumentException(
                                "Could not find region for AZ " + node.cloudInfo.az));

            if (!taskParams().machineImages.containsKey(region.uuid)) {
              throw new IllegalArgumentException(
                  "No VM image was specified for region " + node.cloudInfo.region);
            }

            nodeToRegion.putIfAbsent(node.nodeUuid, region.uuid);
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
        System.out.println("CERT1 " + universe.getUniverseDetails().nodePrefix);
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
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);
      UserIntent primIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

      // Check if the combination of taskType and upgradeOption are compatible.
      verifyParams(universe, primIntent);

      // Get the nodes that need to be upgraded.
      // Left element is master and right element is tserver.
      ImmutablePair<List<NodeDetails>, List<NodeDetails>> nodes =
          nodesToUpgrade(universe, primIntent);

      // Create all the necessary subtasks required for the required taskType and upgradeOption
      // combination.
      createServerUpgradeTasks(nodes.getLeft(), nodes.getRight());

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {} with error={}.", getName(), t);

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
    log.info("Finished {} task.", getName());
  }

  // Find the master leader and move it to the end of the list.
  private List<NodeDetails> sortMastersInRestartOrder(
      String leaderMasterAddress, List<NodeDetails> nodes) {
    if (nodes.isEmpty()) {
      return nodes;
    }
    return nodes
        .stream()
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
    return nodes
        .stream()
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

  private SubTaskGroup createRootVolumeReplacementTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("ReplaceRootVolume", executor);
    ReplaceRootVolume.Params replaceParams = new ReplaceRootVolume.Params();
    replaceParams.nodeName = node.nodeName;
    replaceParams.azUuid = node.azUuid;
    replaceParams.universeUUID = taskParams().universeUUID;
    replaceParams.bootDisksPerZone = this.replacementRootVolumes;

    ReplaceRootVolume replaceDiskTask = createTask(ReplaceRootVolume.class);
    replaceDiskTask.initialize(replaceParams);
    subTaskGroup.addTask(replaceDiskTask);

    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createRootVolumeCreationTasks(List<NodeDetails> nodes) {
    Map<UUID, List<NodeDetails>> rootVolumesPerAZ =
        nodes.stream().collect(Collectors.groupingBy(n -> n.azUuid));
    SubTaskGroup subTaskGroup = new SubTaskGroup("CreateRootVolumes", executor);

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
                        e.getValue()
                            .stream()
                            .filter(n -> !machineImage.equals(n.machineImage))
                            .count();
              }

              if (numVolumes == 0) {
                log.info("Nothing to upgrade in AZ {}", node.cloudInfo.az);
                return;
              }

              CreateRootVolumes.Params params = new CreateRootVolumes.Params();
              UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
              fillSetupParamsForNode(params, userIntent, node);
              params.numVolumes = numVolumes;
              params.machineImage = machineImage;
              params.bootDisksPerZone = replacementRootVolumes;

              log.info(
                  "Creating {} root volumes using {} in AZ {}",
                  params.numVolumes,
                  params.machineImage,
                  node.cloudInfo.az);

              CreateRootVolumes task = createTask(CreateRootVolumes.class);
              task.initialize(params);
              subTaskGroup.addTask(task);
            });
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createNodeDetailsUpdateTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("UpdateNodeDetails", executor);
    UpdateNodeDetails.Params updateNodeDetailsParams = new UpdateNodeDetails.Params();
    updateNodeDetailsParams.universeUUID = taskParams().universeUUID;
    updateNodeDetailsParams.azUuid = node.azUuid;
    updateNodeDetailsParams.nodeName = node.nodeName;
    updateNodeDetailsParams.details = node;

    UpdateNodeDetails updateNodeTask = createTask(UpdateNodeDetails.class);
    updateNodeTask.initialize(updateNodeDetailsParams);
    updateNodeTask.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addTask(updateNodeTask);

    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  private void createServerUpgradeTasks(
      List<NodeDetails> masterNodes, List<NodeDetails> tServerNodes) {
    createPreUpgradeTasks(masterNodes, tServerNodes);
    createUpgradeTasks(masterNodes, tServerNodes, UpgradeIteration.Round1);
    createMetadataUpdateTasks();
    createUpgradeTasks(masterNodes, tServerNodes, UpgradeIteration.Round2);
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

  private void createUpgradeTasks(
      List<NodeDetails> masterNodes,
      List<NodeDetails> tServerNodes,
      UpgradeIteration upgradeIteration) {
    // Currently two round upgrade is needed only for ToggleTls
    if (upgradeIteration == UpgradeIteration.Round2
        && taskParams().taskType != UpgradeTaskType.ToggleTls) {
      return;
    }

    if (taskParams().taskType != UpgradeTaskType.VMImage) {
      UpgradeParams.UpgradeOption upgradeOption = taskParams().upgradeOption;
      if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
        int nodeToNodeChange =
            getNodeToNodeChangeForToggleTls(
                Universe.getOrBadRequest(taskParams().universeUUID)
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

      // Common subtasks
      if (masterNodes != null && !masterNodes.isEmpty()) {
        createAllUpgradeTasks(masterNodes, ServerType.MASTER, upgradeIteration, upgradeOption);
      }
      if (tServerNodes != null && !tServerNodes.isEmpty()) {
        createAllUpgradeTasks(tServerNodes, ServerType.TSERVER, upgradeIteration, upgradeOption);
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

        List<UniverseDefinitionTaskBase.ServerType> processTypes = new ArrayList<>();
        if (node.isMaster) processTypes.add(ServerType.MASTER);
        if (node.isTserver) processTypes.add(ServerType.TSERVER);

        processTypes.forEach(
            processType ->
                createServerControlTask(node, processType, "stop")
                    .setSubTaskGroupType(subGroupType));
        createRootVolumeReplacementTask(node).setSubTaskGroupType(subGroupType);

        List<NodeDetails> nodeList = Collections.singletonList(node);

        createSetupServerTasks(nodeList, true)
            .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
        createConfigureServerTasks(nodeList, false /* isShell */, false, false)
            .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

        processTypes.forEach(
            processType -> {
              createGFlagsOverrideTasks(nodeList, processType);
              createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
              createWaitForServersTasks(new HashSet<NodeDetails>(nodeList), processType);
              createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
                  .setSubTaskGroupType(subGroupType);
            });
        createWaitForKeyInMemoryTask(node);

        node.machineImage = machineImage;
        createNodeDetailsUpdateTask(node).setSubTaskGroupType(subGroupType);
      }
    }
  }

  private void createPostUpgradeTasks() {
    if (taskParams().taskType == UpgradeTaskType.Software) {
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
      UpgradeParams.UpgradeOption upgradeOption) {
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
          createSingleNodeUpgradeTasks(node, processType, upgradeIteration);
        }
        if (loadbalancerOff) {
          createLoadBalancerStateChangeTask(true /*enable*/)
              .setSubTaskGroupType(getTaskSubGroupType());
          loadbalancerOff = false;
        }
        break;
      case NON_ROLLING_UPGRADE:
        createMultipleNonRollingNodeUpgradeTasks(nodes, processType, upgradeIteration);
        createWaitForServersTasks(nodes, processType)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        break;
      case NON_RESTART_UPGRADE:
        createNonRestartUpgradeTasks(nodes, processType, upgradeIteration);
    }
  }

  // This is used for rolling upgrade, which is done per node in the universe.
  private void createSingleNodeUpgradeTasks(
      NodeDetails node, ServerType processType, UpgradeIteration upgradeIteration) {
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
      createSoftwareInstallTasks(Collections.singletonList(node), processType);
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

    createServerControlTask(node, processType, "start").setSubTaskGroupType(subGroupType);
    createWaitForServersTasks(new HashSet<>(Collections.singletonList(node)), processType)
        .setSubTaskGroupType(subGroupType);
    createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
        .setSubTaskGroupType(subGroupType);
    createWaitForKeyInMemoryTask(node).setSubTaskGroupType(subGroupType);
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
                  : taskParams().tserverGFlags,
              false)
          .setSubTaskGroupType(subGroupType);
    } else if (taskParams().taskType == UpgradeTaskType.ToggleTls) {
      Map<String, String> gflags = new HashMap<>();
      gflags.put(
          "allow_insecure_connections",
          upgradeIteration == UpgradeIteration.Round1 ? "true" : "false");
      createSetFlagInMemoryTasks(nodes, processType, true, gflags, false)
          .setSubTaskGroupType(subGroupType);
    }

    createSetNodeStateTasks(nodes, NodeDetails.NodeState.Live).setSubTaskGroupType(subGroupType);
  }

  // This is used for non-rolling upgrade, where each operation is done in parallel across all
  // the provided nodes per given process type.
  private void createMultipleNonRollingNodeUpgradeTasks(
      List<NodeDetails> nodes, ServerType processType, UpgradeIteration upgradeIteration) {
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
      createSoftwareInstallTasks(nodes, processType);
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
      case Restart:
        return SubTaskGroupType.StoppingNodeProcesses;
      case ToggleTls:
        return SubTaskGroupType.ToggleTls;
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

  private void createCertUpdateTasks(List<NodeDetails> nodes) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.RotatingCert, taskParams().nodePrefix);
    SubTaskGroup rotateCertGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      rotateCertGroup.addTask(
          getConfigureTask(
              node, ServerType.TSERVER, UpgradeTaskType.Certs, UpgradeTaskSubType.None));
    }
    rotateCertGroup.setSubTaskGroupType(SubTaskGroupType.RotatingCert);
    subTaskGroupQueue.add(rotateCertGroup);
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
    SubTaskGroup copyCertGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      copyCertGroup.addTask(
          getConfigureTask(
              node, ServerType.TSERVER, UpgradeTaskType.ToggleTls, UpgradeTaskSubType.CopyCerts));
    }
    copyCertGroup.setSubTaskGroupType(SubTaskGroupType.ToggleTls);
    subTaskGroupQueue.add(copyCertGroup);
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
    SubTaskGroup taskGroup = new SubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      taskGroup.addTask(
          getConfigureTask(
              node,
              processType,
              UpgradeTaskType.ToggleTls,
              upgradeIteration == UpgradeIteration.Round1
                  ? UpgradeTaskSubType.Round1GFlagsUpdate
                  : UpgradeTaskSubType.Round2GFlagsUpdate));
    }
    taskGroup.setSubTaskGroupType(SubTaskGroupType.ToggleTls);
    subTaskGroupQueue.add(taskGroup);
  }

  private void createUniverseSetTlsParamsTask() {
    SubTaskGroup taskGroup = new SubTaskGroup("UniverseSetTlsParams", executor);

    UniverseSetTlsParams.Params params = new UniverseSetTlsParams.Params();
    params.universeUUID = taskParams().universeUUID;
    params.enableNodeToNodeEncrypt = taskParams().enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = taskParams().enableClientToNodeEncrypt;
    params.allowInsecure = taskParams().allowInsecure;
    params.rootCA = taskParams().rootCA;

    UniverseSetTlsParams task = createTask(UniverseSetTlsParams.class);
    task.initialize(params);
    taskGroup.addTask(task);

    taskGroup.setSubTaskGroupType(SubTaskGroupType.ToggleTls);
    subTaskGroupQueue.add(taskGroup);
  }

  private int getSleepTimeForProcess(ServerType processType) {
    return processType == ServerType.MASTER
        ? taskParams().sleepAfterMasterRestartMillis
        : taskParams().sleepAfterTServerRestartMillis;
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
    } else if (type == UpgradeTaskType.ToggleTls) {
      params.enableNodeToNodeEncrypt = taskParams().enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = taskParams().enableClientToNodeEncrypt;
      params.allowInsecure = taskParams().allowInsecure;
      params.rootCA = taskParams().rootCA;
      params.nodeToNodeChange = getNodeToNodeChangeForToggleTls(userIntent, taskParams());
    }

    if (userIntent.providerType.equals(Common.CloudType.onprem)) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    // Create the Ansible task to get the server info.
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);

    return task;
  }
}
