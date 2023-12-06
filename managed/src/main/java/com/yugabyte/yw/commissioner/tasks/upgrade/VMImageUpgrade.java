// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class VMImageUpgrade extends UpgradeTaskBase {

  private final Map<UUID, List<String>> replacementRootVolumes = new ConcurrentHashMap<>();

  private final RuntimeConfGetter confGetter;
  private final XClusterUniverseService xClusterUniverseService;
  private final ImageBundleUtil imageBundleUtil;

  @Inject
  protected VMImageUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      RuntimeConfGetter confGetter,
      XClusterUniverseService xClusterUniverseService,
      ImageBundleUtil imageBundleUtil) {
    super(baseTaskDependencies);
    this.confGetter = confGetter;
    this.xClusterUniverseService = xClusterUniverseService;
    this.imageBundleUtil = imageBundleUtil;
  }

  @Override
  protected VMImageUpgradeParams taskParams() {
    return (VMImageUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.Invalid;
  }

  @Override
  public NodeState getNodeState() {
    return null;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    taskParams().verifyParams(getUniverse());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Set<NodeDetails> nodeSet = fetchAllNodes(taskParams().upgradeOption);

          String newVersion = taskParams().ybSoftwareVersion;
          if (taskParams().isSoftwareUpdateViaVm) {
            createCheckUpgradeTask(newVersion).setSubTaskGroupType(getTaskSubGroupType());
          }

          // Create task sequence for VM Image upgrade
          createVMImageUpgradeTasks(nodeSet);

          if (taskParams().isSoftwareUpdateViaVm) {
            // Promote Auto flags on compatible versions.
            if (confGetter.getConfForScope(getUniverse(), UniverseConfKeys.promoteAutoFlag)
                && CommonUtils.isAutoFlagSupported(newVersion)) {
              createCheckSoftwareVersionTask(nodeSet, newVersion)
                  .setSubTaskGroupType(getTaskSubGroupType());
              createPromoteAutoFlagsAndLockOtherUniversesForUniverseSet(
                  Collections.singleton(taskParams().getUniverseUUID()),
                  Collections.singleton(taskParams().getUniverseUUID()),
                  xClusterUniverseService,
                  new HashSet<>(),
                  getUniverse(),
                  newVersion);
            }

            // Update software version in the universe metadata.
            createUpdateSoftwareVersionTask(newVersion, true /*isSoftwareUpdateViaVm*/)
                .setSubTaskGroupType(getTaskSubGroupType());
          }

          createMarkUniverseForHealthScriptReUploadTask();
        });
  }

  private void createVMImageUpgradeTasks(Set<NodeDetails> nodes) {
    createRootVolumeCreationTasks(nodes).setSubTaskGroupType(getTaskSubGroupType());

    Map<UUID, UUID> clusterToImageBundleMap = new HashMap<>();
    for (NodeDetails node : nodes) {
      UUID region = taskParams().nodeToRegion.get(node.nodeUuid);
      String machineImage = "";
      String sshUserOverride = "";
      Integer sshPortOverride = null;
      if (taskParams().imageBundleUUID != null) {
        ImageBundle.NodeProperties toOverwriteNodeProperties =
            imageBundleUtil.getNodePropertiesOrFail(
                taskParams().imageBundleUUID, node.cloudInfo.region, node.cloudInfo.cloud);
        machineImage = toOverwriteNodeProperties.getMachineImage();
        sshUserOverride = toOverwriteNodeProperties.getSshUser();
        sshPortOverride = toOverwriteNodeProperties.getSshPort();
      } else {
        // Backward compatiblity.
        machineImage = taskParams().machineImages.get(region);
        sshUserOverride = taskParams().sshUserOverrideMap.get(region);
      }
      log.info(
          "Upgrading universe nodes to use vm image {}, having ssh user {} & port {}",
          machineImage,
          sshUserOverride,
          sshPortOverride);

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
      if (getUniverse().isYbcEnabled()) processTypes.add(ServerType.CONTROLLER);

      // The node is going to be stopped. Ignore error because of previous error due to
      // possibly detached root volume.
      processTypes.forEach(
          processType ->
              createServerControlTask(
                      node, processType, "stop", params -> params.isIgnoreError = true)
                  .setSubTaskGroupType(getTaskSubGroupType()));

      createRootVolumeReplacementTask(node).setSubTaskGroupType(getTaskSubGroupType());
      node.machineImage = machineImage;
      if (StringUtils.isNotBlank(sshUserOverride)) {
        node.sshUserOverride = sshUserOverride;
      }
      if (sshPortOverride != null) {
        node.sshPortOverride = sshPortOverride;
      }

      node.ybPrebuiltAmi =
          taskParams().vmUpgradeTaskType == VmUpgradeTaskType.VmUpgradeWithCustomImages;
      List<NodeDetails> nodeList = Collections.singletonList(node);
      createInstallNodeAgentTasks(nodeList).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      createWaitForNodeAgentTasks(nodeList).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      createHookProvisionTask(nodeList, TriggerType.PreNodeProvision);
      createSetupServerTasks(nodeList, p -> p.vmUpgradeTaskType = taskParams().vmUpgradeTaskType)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      createHookProvisionTask(nodeList, TriggerType.PostNodeProvision);
      createConfigureServerTasks(
              nodeList, params -> params.vmUpgradeTaskType = taskParams().vmUpgradeTaskType)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Copy the source root certificate to the node.
      createTransferXClusterCertsCopyTasks(
          Collections.singleton(node), getUniverse(), SubTaskGroupType.InstallingSoftware);

      processTypes.forEach(
          processType -> {
            if (processType.equals(ServerType.CONTROLLER)) {
              createStartYbcTasks(Arrays.asList(node)).setSubTaskGroupType(getTaskSubGroupType());

              // Wait for yb-controller to be responsive on each node.
              createWaitForYbcServerTask(new HashSet<>(Arrays.asList(node)))
                  .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
            } else {
              // Todo: remove the following subtask.
              // We have an issue where the tserver gets running once the VM with the new image is
              // up.
              createServerControlTask(
                  node, processType, "stop", params -> params.isIgnoreError = true);

              createGFlagsOverrideTasks(
                  nodeList,
                  processType,
                  false /*isMasterInShellMode*/,
                  taskParams().vmUpgradeTaskType,
                  false /*ignoreUseCustomImageConfig*/);
              createServerControlTask(node, processType, "start")
                  .setSubTaskGroupType(getTaskSubGroupType());
              createWaitForServersTasks(new HashSet<>(nodeList), processType);
              createWaitForServerReady(node, processType, getSleepTimeForProcess(processType))
                  .setSubTaskGroupType(getTaskSubGroupType());
              // If there are no universe keys on the universe, it will have no effect.
              if (processType == ServerType.MASTER
                  && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
                createSetActiveUniverseKeysTask().setSubTaskGroupType(getTaskSubGroupType());
              }
            }
          });

      createWaitForKeyInMemoryTask(node);
      if (taskParams().imageBundleUUID != null) {
        if (!clusterToImageBundleMap.containsKey(node.placementUuid)) {
          clusterToImageBundleMap.put(node.placementUuid, taskParams().imageBundleUUID);
        }
      }
      createNodeDetailsUpdateTask(node, !taskParams().isSoftwareUpdateViaVm)
          .setSubTaskGroupType(getTaskSubGroupType());
    }

    // Update the imageBundleUUID in the cluster -> userIntent
    if (!clusterToImageBundleMap.isEmpty()) {
      clusterToImageBundleMap.forEach(
          (clusterUUID, imageBundleUUID) -> {
            createClusterUserIntentUpdateTask(clusterUUID, imageBundleUUID)
                .setSubTaskGroupType(getTaskSubGroupType());
          });
    }
    // Delete after all the disks are replaced.
    createDeleteRootVolumesTasks(getUniverse(), nodes, null /* volume Ids */)
        .setSubTaskGroupType(getTaskSubGroupType());
  }

  private SubTaskGroup createRootVolumeCreationTasks(Collection<NodeDetails> nodes) {
    Map<UUID, List<NodeDetails>> rootVolumesPerAZ =
        nodes.stream().collect(Collectors.groupingBy(n -> n.azUuid));
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateRootVolumes");

    rootVolumesPerAZ.forEach(
        (key, value) -> {
          NodeDetails node = value.get(0);
          UUID region = taskParams().nodeToRegion.get(node.nodeUuid);
          String updatedMachineImage = "";
          if (taskParams().imageBundleUUID != null) {
            ImageBundle.NodeProperties toOverwriteNodeProperties =
                imageBundleUtil.getNodePropertiesOrFail(
                    taskParams().imageBundleUUID, node.cloudInfo.region, node.cloudInfo.cloud);
            updatedMachineImage = toOverwriteNodeProperties.getMachineImage();
          } else {
            // Backward compatiblity.
            updatedMachineImage = taskParams().machineImages.get(region);
          }
          final String machineImage = updatedMachineImage;
          int numVolumes = value.size();

          if (!taskParams().forceVMImageUpgrade) {
            numVolumes =
                (int) value.stream().filter(n -> !machineImage.equals(n.machineImage)).count();
          }

          if (numVolumes == 0) {
            log.info("Nothing to upgrade in AZ {}", node.cloudInfo.az);
            return;
          }

          CreateRootVolumes.Params params = new CreateRootVolumes.Params();
          Cluster cluster = taskParams().getClusterByUuid(node.placementUuid);
          if (cluster == null) {
            throw new IllegalArgumentException(
                "No cluster available with UUID: " + node.placementUuid);
          }
          UserIntent userIntent = cluster.userIntent;
          fillCreateParamsForNode(params, userIntent, node);
          params.numVolumes = numVolumes;
          params.setMachineImage(machineImage);
          params.bootDisksPerZone = this.replacementRootVolumes;

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

  private SubTaskGroup createRootVolumeReplacementTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("ReplaceRootVolume");
    ReplaceRootVolume.Params replaceParams = new ReplaceRootVolume.Params();
    replaceParams.nodeName = node.nodeName;
    replaceParams.azUuid = node.azUuid;
    replaceParams.setUniverseUUID(taskParams().getUniverseUUID());
    replaceParams.bootDisksPerZone = this.replacementRootVolumes;

    ReplaceRootVolume replaceDiskTask = createTask(ReplaceRootVolume.class);
    replaceDiskTask.initialize(replaceParams);
    subTaskGroup.addSubTask(replaceDiskTask);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
