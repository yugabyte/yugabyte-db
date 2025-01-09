// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeState;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
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
@Retryable
@Abortable
public class VMImageUpgrade extends UpgradeTaskBase {

  private final Map<UUID, List<String>> replacementRootVolumes = new ConcurrentHashMap<>();
  private final Map<UUID, String> replacementRootDevices = new ConcurrentHashMap<>();

  private final RuntimeConfGetter confGetter;
  private final ImageBundleUtil imageBundleUtil;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  protected VMImageUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      RuntimeConfGetter confGetter,
      ImageBundleUtil imageBundleUtil,
      XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies);
    this.confGetter = confGetter;
    this.imageBundleUtil = imageBundleUtil;
    this.xClusterUniverseService = xClusterUniverseService;
  }

  @Override
  protected VMImageUpgradeParams taskParams() {
    return (VMImageUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.OSPatching;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.VMImageUpgrade;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    Set<NodeDetails> nodeSet = fetchNodesForCluster();
    String newVersion = taskParams().ybSoftwareVersion;
    if (taskParams().isSoftwareUpdateViaVm) {
      createCheckUpgradeTask(newVersion).setSubTaskGroupType(getTaskSubGroupType());
      if (confGetter.getConfForScope(getUniverse(), UniverseConfKeys.promoteAutoFlag)
          && CommonUtils.isAutoFlagSupported(newVersion)) {
        createCheckSoftwareVersionTask(nodeSet, newVersion)
            .setSubTaskGroupType(getTaskSubGroupType());
      }
    }
    addBasicPrecheckTasks();
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodesForClustersInParams();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          MastersAndTservers nodes = getNodesToBeRestarted();
          Set<NodeDetails> nodeSet = toOrderedSet(nodes.asPair());

          String newVersion = taskParams().ybSoftwareVersion;

          // Create task sequence for VM Image upgrade
          createVMImageUpgradeTasks(nodeSet);

          if (taskParams().isSoftwareUpdateViaVm) {
            // Promote Auto flags on compatible versions.
            if (confGetter.getConfForScope(getUniverse(), UniverseConfKeys.promoteAutoFlag)
                && CommonUtils.isAutoFlagSupported(newVersion)) {
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

  private static class ImageSettings {
    final String machineImage;
    final String sshUserOverride;
    final Integer sshPortOverride;
    final UUID imageBundleUUID;

    private ImageSettings(
        String machineImage,
        String sshUserOverride,
        Integer sshPortOverride,
        UUID imageBundleUUID) {
      this.machineImage = machineImage;
      this.sshUserOverride = sshUserOverride;
      this.sshPortOverride = sshPortOverride;
      this.imageBundleUUID = imageBundleUUID;
    }
  }

  private Map<NodeDetails, ImageSettings> getImageSettingsForNodes(Set<NodeDetails> nodes) {
    Map<NodeDetails, ImageSettings> result = new LinkedHashMap<>();
    UUID imageBundleUUID;
    for (NodeDetails node : nodes) {
      UUID region = taskParams().nodeToRegion.get(node.nodeUuid);
      String machineImage = "";
      String sshUserOverride = "";
      Integer sshPortOverride = null;
      imageBundleUUID = null;
      if (taskParams().imageBundles != null && taskParams().imageBundles.size() > 0) {
        imageBundleUUID = retrieveImageBundleUUID(taskParams().imageBundles, node);
        ImageBundle.NodeProperties toOverwriteNodeProperties =
            imageBundleUtil.getNodePropertiesOrFail(
                imageBundleUUID, node.cloudInfo.region, node.cloudInfo.cloud);
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

      String existingMachineImage = node.machineImage;
      if (StringUtils.isBlank(existingMachineImage)) {
        existingMachineImage = retreiveMachineImageForNode(node);
      }

      if (!taskParams().forceVMImageUpgrade && machineImage.equals(existingMachineImage)) {
        log.info(
            "Skipping node {} as it's already running on {} and force flag is not set",
            node.nodeName,
            machineImage);
        continue;
      }
      result.put(
          node, new ImageSettings(machineImage, sshUserOverride, sshPortOverride, imageBundleUUID));
    }
    return result;
  }

  private void createVMImageUpgradeTasks(Set<NodeDetails> nodes) {
    Map<NodeDetails, ImageSettings> imageSettingsMap = getImageSettingsForNodes(nodes);

    createRootVolumeCreationTasks(imageSettingsMap).setSubTaskGroupType(getTaskSubGroupType());

    Map<UUID, UUID> clusterToImageBundleMap = new HashMap<>();
    Universe universe = getUniverse();
    for (NodeDetails node : imageSettingsMap.keySet()) {
      ImageSettings imageSettings = imageSettingsMap.get(node);
      final UUID imageBundleUUID = imageSettings.imageBundleUUID;
      final String sshUserOverride = imageSettings.sshUserOverride;
      final Integer sshPortOverride = imageSettings.sshPortOverride;
      final String machineImage = imageSettings.machineImage;
      Set<UniverseTaskBase.ServerType> processTypes = new LinkedHashSet<>();
      if (node.isMaster) {
        processTypes.add(ServerType.MASTER);
      }
      if (node.isTserver) {
        processTypes.add(ServerType.TSERVER);
      }
      if (universe.isYbcEnabled()) processTypes.add(ServerType.CONTROLLER);

      createSetNodeStateTask(node, getNodeState());

      createCheckNodesAreSafeToTakeDownTask(
          Collections.singletonList(MastersAndTservers.from(node, processTypes)),
          getTargetSoftwareVersion(),
          false);

      // The node is going to be stopped. Ignore error because of previous error due to
      // possibly detached root volume.
      processTypes.forEach(
          processType ->
              createServerControlTask(
                      node, processType, "stop", params -> params.isIgnoreError = true)
                  .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses));

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
      boolean useYNPProvisioning = confGetter.getGlobalConf(GlobalConfKeys.enableYNPProvisioning);
      // TODO This can be improved to skip already provisioned nodes as there are long running
      // subtasks.
      if (useYNPProvisioning) {
        createSetupYNPTask(nodeList).setSubTaskGroupType(SubTaskGroupType.Provisioning);
        createYNPProvisioningTask(nodeList).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      }
      createInstallNodeAgentTasks(nodeList).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      createWaitForNodeAgentTasks(nodeList).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      createHookProvisionTask(nodeList, TriggerType.PreNodeProvision);
      if (!useYNPProvisioning) {
        createSetupServerTasks(
                nodeList,
                p -> {
                  p.vmUpgradeTaskType = taskParams().vmUpgradeTaskType;
                  p.rebootNodeAllowed = true;
                })
            .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      }
      createHookProvisionTask(nodeList, TriggerType.PostNodeProvision);
      createLocaleCheckTask(nodeList).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      createCheckGlibcTask(
              new ArrayList<>(universe.getNodes()),
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);
      createConfigureServerTasks(
              nodeList, params -> params.vmUpgradeTaskType = taskParams().vmUpgradeTaskType)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

      // Copy the source root certificate to the node.
      createTransferXClusterCertsCopyTasks(
          Collections.singleton(node), universe, SubTaskGroupType.InstallingSoftware);

      processTypes.forEach(
          processType -> {
            if (processType.equals(ServerType.CONTROLLER)) {
              createStartYbcTasks(Arrays.asList(node))
                  .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

              // Wait for yb-controller to be responsive on each node.
              createWaitForYbcServerTask(new HashSet<>(Arrays.asList(node)))
                  .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
            } else {
              long startTime = System.currentTimeMillis();
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
                  .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
              createWaitForServersTasks(new HashSet<>(nodeList), processType);
              createWaitForServerReady(node, processType)
                  .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
              // If there are no universe keys on the universe, it will have no effect.
              if (processType == ServerType.MASTER
                  && EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
                createSetActiveUniverseKeysTask()
                    .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
              }
              createSleepAfterStartupTask(
                  universe.getUniverseUUID(),
                  Collections.singletonList(processType),
                  SetNodeState.getStartKey(node.getNodeName(), getNodeState()));
            }
          });

      createWaitForKeyInMemoryTask(node);
      if (imageBundleUUID != null) {
        if (!clusterToImageBundleMap.containsKey(node.placementUuid)) {
          clusterToImageBundleMap.put(node.placementUuid, imageBundleUUID);
        }
      }
      createSetNodeStateTask(node, NodeState.Live);
      createNodeDetailsUpdateTask(node, !taskParams().isSoftwareUpdateViaVm)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Update the imageBundleUUID in the cluster -> userIntent
    if (!clusterToImageBundleMap.isEmpty()) {
      clusterToImageBundleMap.forEach(
          (clusterUUID, bundleUUID) -> {
            createClusterUserIntentUpdateTask(clusterUUID, bundleUUID)
                .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          });
    }
    // Delete after all the disks are replaced.
    createDeleteRootVolumesTasks(universe, nodes, null /* volume Ids */)
        .setSubTaskGroupType(getTaskSubGroupType());
  }

  private SubTaskGroup createRootVolumeCreationTasks(Map<NodeDetails, ImageSettings> settingsMap) {
    Map<UUID, List<NodeDetails>> rootVolumesPerAZ =
        settingsMap.keySet().stream().collect(Collectors.groupingBy(n -> n.azUuid));
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateRootVolumes");

    rootVolumesPerAZ.forEach(
        (key, value) -> {
          NodeDetails node = value.get(0);
          ImageSettings imageSettings = settingsMap.get(node);

          final String machineImage = imageSettings.machineImage;
          int numVolumes = value.size();

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
          params.rootDevicePerZone = this.replacementRootDevices;

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
    replaceParams.rootDevicePerZone = this.replacementRootDevices;

    ReplaceRootVolume replaceDiskTask = createTask(ReplaceRootVolume.class);
    replaceDiskTask.initialize(replaceParams);
    subTaskGroup.addSubTask(replaceDiskTask);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private UUID retrieveImageBundleUUID(
      List<VMImageUpgradeParams.ImageBundleUpgradeInfo> imageBundles, NodeDetails node) {
    return imageBundles.stream()
        .filter(info -> info.getClusterUuid().equals(node.placementUuid))
        .findFirst()
        .map(VMImageUpgradeParams.ImageBundleUpgradeInfo::getImageBundleUuid)
        .orElse(null);
  }

  private String retreiveMachineImageForNode(NodeDetails node) {
    UUID clusterUuid = node.placementUuid;
    UniverseDefinitionTaskParams.Cluster cluster = getUniverse().getCluster(clusterUuid);
    if (cluster.userIntent.imageBundleUUID != null) {
      ImageBundle.NodeProperties imageBundleProperties =
          imageBundleUtil.getNodePropertiesOrFail(
              cluster.userIntent.imageBundleUUID, node.getRegion(), node.cloudInfo.cloud);
      return imageBundleProperties.getMachineImage();
    }
    return null;
  }
}
