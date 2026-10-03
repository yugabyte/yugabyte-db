// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UpdateOOMServiceState;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunNodeCommand;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckOCIImageEligibility;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Retryable
@Abortable
public class VMImageUpgrade extends UpgradeTaskBase {

  // AZ to node name to volume ID. The name to volume ID assignment makes sure that it is
  // deterministic on retry after a partial failure.
  private final Map<UUID, Map<String, String>> replacementRootVolumes = new ConcurrentHashMap<>();
  private final Map<UUID, String> replacementRootDevices = new ConcurrentHashMap<>();
  // Node UUID -> (mount path -> disk UUID). Shared with YNPProvisioning params so CaptureFstab
  // can populate it before provisioning runs in the same task execution.
  private final Map<UUID, Map<String, String>> deviceMappingByNode = new ConcurrentHashMap<>();

  private final XClusterUniverseService xClusterUniverseService;

  private volatile RuntimeInfo runtimeInfo;

  private volatile boolean enableEarlyoom;
  private volatile UpdateOOMServiceState.EarlyoomEnablementState enablementState;

  @Inject
  protected VMImageUpgrade(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies);
    this.xClusterUniverseService = xClusterUniverseService;
  }

  /** Task runtime progress info. */
  public static class RuntimeInfo {
    @JsonProperty("volumesCreated")
    boolean volumesCreated;

    @JsonProperty("replacementRootVolumes")
    // AZ to node name to volume ID.
    Map<UUID, Map<String, String>> replacementRootVolumes = new ConcurrentHashMap<>();

    @JsonProperty("replacementRootDevices")
    Map<UUID, String> replacementRootDevices = new ConcurrentHashMap<>();

    @JsonProperty("volumeReplacedNodes")
    Set<UUID> volumeReplacedNodes = ConcurrentHashMap.newKeySet();

    @JsonProperty("replacementCompletedNodes")
    Set<UUID> replacementCompletedNodes = ConcurrentHashMap.newKeySet();

    // Node UUID to mount-path -> disk-UUID mapping captured from /etc/fstab before root volume
    // replacement.
    @JsonProperty("deviceMappingByNode")
    Map<UUID, Map<String, String>> deviceMappingByNode = new ConcurrentHashMap<>();
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
      createCheckUpgradeTask(newVersion);
      if (confGetter.getConfForScope(getUniverse(), UniverseConfKeys.promoteAutoFlag)
          && CommonUtils.isAutoFlagSupported(newVersion)) {
        createCheckSoftwareVersionTask(nodeSet, newVersion)
            .setSubTaskGroupType(getTaskSubGroupType());
      }
    }
    addBasicPrecheckTasks();
    createCheckOCIImageEligibilityTask(universe);
    runtimeInfo = getRuntimeInfo(RuntimeInfo.class);
    Customer customer = Customer.get(universe.getCustomerId());

    enablementState =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, universe.getUniverseDetails(), customer);
    log.debug("Earlyoom enablement state {}", enablementState);

    enableEarlyoom =
        enablementState.isInstallationPossible()
            && (universe.getUniverseDetails().additionalServicesStateData == null
                || !universe.getUniverseDetails().additionalServicesStateData.isEarlyoomEnabled())
            && enablementState.isEnableOnUpgrade();

    if (enableEarlyoom) {
      Set<String> nodesWithoutNA =
          universe.getUniverseDetails().nodeDetailsSet.stream()
              .map(n -> new Pair<>(n, nodeUniverseManager.maybeUpgradeAndGetNodeAgent(universe, n)))
              .filter(p -> p.getSecond().isEmpty())
              .map(p -> p.getFirst().nodeName)
              .collect(Collectors.toSet());
      if (!nodesWithoutNA.isEmpty()) {
        log.warn("Cannot install earlyoom: found nodes without node agent: {}", nodesWithoutNA);
        enableEarlyoom = false;
      }
    }
  }

  // TODO(PLAT-22730): also check the images the nodes are running now, not just the target images.
  private void createCheckOCIImageEligibilityTask(Universe universe) {
    Set<NodeDetails> ociNodes =
        toOrderedSet(getNodesToBeRestarted().asPair()).stream()
            .filter(
                n -> universe.getCluster(n.placementUuid).getProviderCloudType(n) == CloudType.oci)
            .collect(Collectors.toCollection(LinkedHashSet::new));
    Map<String, ImageSettings> imageSettingsMap = getImageSettingsForNodes(ociNodes);
    Set<CheckOCIImageEligibility.TargetImage> targetImages =
        ociNodes.stream()
            .filter(n -> imageSettingsMap.containsKey(n.nodeName))
            .map(
                n ->
                    new CheckOCIImageEligibility.TargetImage(
                        universe.getCluster(n.placementUuid).getProviderUUIDForNode(n),
                        n.cloudInfo.region,
                        imageSettingsMap.get(n.nodeName).machineImage))
            .collect(Collectors.toCollection(LinkedHashSet::new));
    if (targetImages.isEmpty()) {
      return;
    }
    doInPrecheckSubTaskGroup(
        "CheckOCIImageEligibility",
        subTaskGroup -> {
          CheckOCIImageEligibility.Params params = new CheckOCIImageEligibility.Params();
          params.setUniverseUUID(taskParams().getUniverseUUID());
          params.targetImages = new ArrayList<>(targetImages);
          CheckOCIImageEligibility task = createTask(CheckOCIImageEligibility.class);
          task.initialize(params);
          subTaskGroup.addSubTask(task);
        });
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodesForClustersInParams();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          MastersAndTservers allNodes = getNodesToBeRestarted();
          LinkedHashSet<NodeDetails> allNodesSet = toOrderedSet(allNodes.asPair());
          Map<String, ImageSettings> imageSettingsMap = getImageSettingsForNodes(allNodesSet);
          LinkedHashSet<NodeDetails> nodeSet =
              allNodesSet.stream()
                  .filter(n -> imageSettingsMap.containsKey(n.nodeName))
                  .filter(n -> !runtimeInfo.replacementCompletedNodes.contains(n.getNodeUuid()))
                  .collect(Collectors.toCollection(LinkedHashSet::new));

          Universe universe = getUniverse();

          boolean deleteCapacityReservation =
              createCapacityReservationsIfNeeded(
                  nodeSet,
                  CapacityReservationUtil.OperationType.OS_UPGRADE,
                  node ->
                      imageSettingsMap.containsKey(node.nodeName)
                          && !runtimeInfo.replacementCompletedNodes.contains(node.getNodeUuid()));

          String newVersion = taskParams().ybSoftwareVersion;
          restoreRuntimeInfoAndPrepareVolumes(nodeSet, imageSettingsMap);

          Map<String, UUID> nodeToImageBundleMap = new HashMap<>();
          boolean reconfigureMaster =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor > 1;

          UpgradeContext context =
              UpgradeContext.builder()
                  .runBeforeStopping(false)
                  .processInactiveMaster(false)
                  // Since the update is supposed to be quite long-running.
                  .reconfigureMaster(reconfigureMaster)
                  .nodesAreStopped(true)
                  .preAction(
                      node -> {
                        if (!runtimeInfo.volumeReplacedNodes.contains(node.getNodeUuid())) {
                          // Capture /etc/fstab before the root volume is replaced (old root is
                          // detached after).
                          if (!runtimeInfo.deviceMappingByNode.containsKey(node.getNodeUuid())) {
                            createCaptureFstabTask(universe, node)
                                .setSubTaskGroupType(getTaskSubGroupType());
                          }
                        }
                      })
                  .postAction(
                      node -> {
                        createUpdateUniverseFieldsTask(
                                u -> {
                                  NodeDetails nodeDetails = u.getNode(node.nodeName);
                                  if (nodeDetails != null) {
                                    nodeDetails.machineImage = node.machineImage;
                                    nodeDetails.sshUserOverride = node.sshUserOverride;
                                    nodeDetails.sshPortOverride = node.sshPortOverride;
                                    nodeDetails.ybPrebuiltAmi = node.ybPrebuiltAmi;
                                    if (!taskParams().isSoftwareUpdateViaVm) {
                                      u.updateConfig(
                                          ImmutableMap.of(
                                              Universe.USE_CUSTOM_IMAGE,
                                              Boolean.toString(
                                                  u.getUniverseDetails().nodeDetailsSet.stream()
                                                      .allMatch(n -> n.ybPrebuiltAmi))));
                                    }
                                  }
                                })
                            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse)
                            .setAfterGroupRunListener(
                                g ->
                                    updateRuntimeInfo(
                                        RuntimeInfo.class,
                                        info ->
                                            info.replacementCompletedNodes.add(
                                                node.getNodeUuid())));
                      })
                  .build();

          createRollingNodesUpgradeTaskFlow(
              (nodes, processTypes) ->
                  createVMImageUpgradeTasks(
                      imageSettingsMap, nodes, processTypes, nodeToImageBundleMap),
              nodeSet,
              context,
              taskParams().isYbcInstalled());

          createPersistCpuCgroupConfiguredTask(universe);

          // Update the imageBundleUUID in the cluster -> userIntent
          if (!nodeToImageBundleMap.isEmpty()) {
            createClusterUserIntentUpdateTask(nodeToImageBundleMap)
                .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          }
          // Delete after all the disks are replaced.
          createDeleteRootVolumesTasks(universe, allNodesSet, null /* volume Ids */)
              .setSubTaskGroupType(getTaskSubGroupType());

          if (deleteCapacityReservation) {
            createDeleteCapacityReservationTask();
          }

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

          if (enableEarlyoom) {
            AdditionalServicesStateData servicesStateData =
                universe.getUniverseDetails().additionalServicesStateData;
            if (servicesStateData == null) {
              servicesStateData = new AdditionalServicesStateData();
              servicesStateData.setEarlyoomConfig(enablementState.getConfig());
            }
            servicesStateData.setEarlyoomEnabled(true);

            createConfigureOOMServiceSubtasks(servicesStateData, universe.getNodes());
            AdditionalServicesStateData finalServicesStateData = servicesStateData;
            createUpdateUniverseFieldsTask(
                u -> u.getUniverseDetails().additionalServicesStateData = finalServicesStateData);
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

  private void restoreRuntimeInfoAndPrepareVolumes(
      Collection<NodeDetails> nodes, Map<String, ImageSettings> imageSettingsMap) {
    // Restore mount-path -> UUID mappings from a prior attempt so YNPProvisioning can remount.
    runtimeInfo.deviceMappingByNode.forEach(
        (nodeUuid, mapping) ->
            deviceMappingByNode
                .computeIfAbsent(nodeUuid, k -> new ConcurrentHashMap<>())
                .putAll(mapping));
    if (runtimeInfo.volumesCreated) {
      replacementRootDevices.putAll(runtimeInfo.replacementRootDevices);
      replacementRootVolumes.putAll(runtimeInfo.replacementRootVolumes);
    } else {
      createRootVolumeCreationTasks(nodes, imageSettingsMap)
          .setSubTaskGroupType(getTaskSubGroupType())
          .setAfterGroupRunListener(
              g ->
                  updateRuntimeInfo(
                      RuntimeInfo.class,
                      info -> {
                        info.replacementRootDevices = replacementRootDevices;
                        info.replacementRootVolumes = replacementRootVolumes;
                        info.volumesCreated = true;
                      }));
    }
  }

  private Map<String, ImageSettings> getImageSettingsForNodes(Set<NodeDetails> nodes) {
    Universe universe = getUniverse();
    Map<String, ImageSettings> result = new LinkedHashMap<>();
    UUID imageBundleUUID;
    for (NodeDetails node : nodes) {
      UUID region = taskParams().nodeToRegion.get(node.nodeUuid);
      String machineImage = "";
      String sshUserOverride = "";
      Integer sshPortOverride = null;
      imageBundleUUID = null;
      if (taskParams().imageBundles != null && taskParams().imageBundles.size() > 0) {
        Optional<VMImageUpgradeParams.ImageBundleUpgradeInfo> imageBundleUpgradeInfo =
            VMImageUpgradeParams.findForNode(taskParams().imageBundles, universe, node);
        if (!imageBundleUpgradeInfo.isPresent()) {
          continue;
        }
        imageBundleUUID = imageBundleUpgradeInfo.get().getImageBundleUuid();
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
          "Upgrading node {} to use vm image {}, having ssh user {} & port {}",
          node.nodeName,
          machineImage,
          sshUserOverride,
          sshPortOverride);

      String existingMachineImage = node.machineImage;
      if (StringUtils.isBlank(existingMachineImage)) {
        existingMachineImage = retreiveMachineImageForNode(node);
      }

      if (!taskParams().forceVMImageUpgrade
          && StringUtils.equals(machineImage, existingMachineImage)) {
        log.info(
            "Skipping node {} as it's already running on {} and force flag is not set",
            node.nodeName,
            machineImage);
        continue;
      }
      result.put(
          node.nodeName,
          new ImageSettings(machineImage, sshUserOverride, sshPortOverride, imageBundleUUID));
    }
    return result;
  }

  private void createVMImageUpgradeTasks(
      Map<String, ImageSettings> imageSettingsMap,
      Collection<NodeDetails> nodes,
      Set<ServerType> processTypes,
      Map<String, UUID> nodeToImageBundleMap) {
    if (nodes.isEmpty()) {
      return;
    }
    Universe universe = getUniverse();
    NodeDetails anyNode = nodes.iterator().next();
    Cluster cluster = universe.getCluster(anyNode.placementUuid);
    boolean isLocal = cluster.getProviderCloudType(anyNode) == CloudType.local;

    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    Function<NodeDetails, Provider> providerGetter = Util.getProviderGetter(universe);

    Set<NodeDetails> nodesToReplaceRootVolume =
        nodes.stream()
            .filter(node -> !runtimeInfo.volumeReplacedNodes.contains(node.getNodeUuid()))
            .collect(Collectors.toSet());
    addParallelTasks(
            nodesToReplaceRootVolume,
            node -> {
              ImageSettings imageSettings = imageSettingsMap.get(node.nodeName);
              return createRootVolumeReplacementTask(node, imageSettings.sshPortOverride);
            },
            "ReplaceRootVolume",
            getTaskSubGroupType())
        .setAfterTaskRunHandler(
            (t, e) -> {
              String nodeName = t.getTaskParams().get("nodeName").textValue();
              NodeDetails node = Util.findByName(nodes, nodeName);
              if (node == null) {
                throw new IllegalStateException("Failed to find node by name " + nodeName);
              }
              if (e == null) {
                updateRuntimeInfo(
                    RuntimeInfo.class, info -> info.volumeReplacedNodes.add(node.getNodeUuid()));
              }
              return e;
            });

    // Updating image settings on nodes.
    for (NodeDetails node : nodes) {
      ImageSettings imageSettings = imageSettingsMap.get(node.nodeName);
      final UUID imageBundleUUID = imageSettings.imageBundleUUID;
      final String sshUserOverride = imageSettings.sshUserOverride;
      final Integer sshPortOverride = imageSettings.sshPortOverride;
      final String machineImage = imageSettings.machineImage;

      node.machineImage = machineImage;
      if (StringUtils.isNotBlank(sshUserOverride)) {
        node.sshUserOverride = sshUserOverride;
      }
      if (sshPortOverride != null) {
        node.sshPortOverride = sshPortOverride;
      }

      node.ybPrebuiltAmi =
          taskParams().vmUpgradeTaskType == VmUpgradeTaskType.VmUpgradeWithCustomImages;

      if (imageBundleUUID != null) {
        nodeToImageBundleMap.put(node.nodeName, imageBundleUUID);
      }

      // Persist updated node SSH fields to DB before provisioning subtasks, as subtasks
      // like SetupYNP and YNPProvisioning reload the node from DB and need the target
      // image bundle's SSH port/user to connect to the node after root volume replacement.
      // Note: machineImage is intentionally NOT persisted here -- doing so would cause
      // getImageSettingsForNodes to skip this node on retry (image already matches target).
      createUpdateUniverseFieldsTask(
              u -> {
                NodeDetails nodeDetails = u.getNode(node.nodeName);
                if (nodeDetails != null) {
                  nodeDetails.sshUserOverride = node.sshUserOverride;
                  nodeDetails.sshPortOverride = node.sshPortOverride;
                  nodeDetails.ybPrebuiltAmi = node.ybPrebuiltAmi;
                }
              })
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    createHookProvisionTask(nodes, TriggerType.PreNodeProvision);
    if (!isLocal) {
      createSetupYNPTask(universe, nodes).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      boolean isYbPrebuiltImage =
          !shouldInstallDbSoftware(
              universe, false /*ignoreUseCustomImageConfig*/, taskParams().vmUpgradeTaskType);
      createYNPProvisioningTask(
              universe,
              nodes,
              (n, p) -> {
                p.isYbPrebuiltImage = isYbPrebuiltImage;
                p.isDataPresent = true;
                p.pathToUUIDMapping =
                    deviceMappingByNode.computeIfAbsent(
                        n.getNodeUuid(), k -> new ConcurrentHashMap<>());
              })
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);

      if (universe.getUniverseDetails().fipsEnabled) {
        // The root volume was replaced, so this is a fresh OS that provisioning has just put
        // into FIPS mode - which only takes effect on reboot.
        createRebootTasks(nodes, false /* isHardReboot */)
            .setSubTaskGroupType(SubTaskGroupType.Provisioning);
      }
    }
    createInstallNodeAgentTasks(universe, nodes).setSubTaskGroupType(SubTaskGroupType.Provisioning);
    createWaitForNodeAgentTasks(nodes).setSubTaskGroupType(SubTaskGroupType.Provisioning);
    if (isLocal) {
      createSetupServerTasks(
              nodes,
              p -> {
                p.vmUpgradeTaskType = taskParams().vmUpgradeTaskType;
                p.rebootNodeAllowed = true;
              })
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
    } else {
      createSetNodeStatusTasks(nodes, NodeStatus.builder().nodeState(NodeState.ServerSetup).build())
          .setSubTaskGroupType(SubTaskGroupType.Provisioning);
    }
    createHookProvisionTask(nodes, TriggerType.PostNodeProvision);
    createLocaleCheckTask(nodes).setSubTaskGroupType(SubTaskGroupType.Provisioning);
    createCheckGlibcTask(
            new ArrayList<>(universe.getNodes()), primaryCluster.userIntent.ybSoftwareVersion)
        .setSubTaskGroupType(SubTaskGroupType.Provisioning);

    createConfigureServerTasks(
            nodes,
            params -> {
              NodeDetails node = Util.findByName(nodes, params.nodeName);
              Provider provider = providerGetter.apply(node);
              params.vmUpgradeTaskType = taskParams().vmUpgradeTaskType;
              params.configureCgroupOverride =
                  Util.configureCgroup(cluster.userIntent, provider, true, confGetter);
            })
        .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

    // Copy the source root certificate to the node.
    createTransferXClusterCertsCopyTasks(nodes, universe, SubTaskGroupType.InstallingSoftware);

    processTypes.forEach(
        processType -> {
          if (!processType.equals(ServerType.CONTROLLER)) {
            // Todo: remove the following subtask.
            // We have an issue where the tserver gets running once the VM with the new image is
            // up.
            nodes.forEach(
                node ->
                    createServerControlTask(
                        node, processType, "stop", params -> params.isIgnoreError = true));

            createGFlagsOverrideTasks(
                nodes,
                processType,
                false /*isMasterInShellMode*/,
                taskParams().vmUpgradeTaskType,
                false /*ignoreUseCustomImageConfig*/);
          }
        });
  }

  private SubTaskGroup createRootVolumeCreationTasks(
      Collection<NodeDetails> nodes, Map<String, ImageSettings> settingsMap) {
    Map<UUID, List<NodeDetails>> rootVolumesPerAZ =
        nodes.stream().collect(Collectors.groupingBy(n -> n.azUuid));
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateRootVolumes", getTaskSubGroupType());

    rootVolumesPerAZ.forEach(
        (key, value) -> {
          NodeDetails node = value.get(0);
          ImageSettings imageSettings = settingsMap.get(node.nodeName);

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
          params.bootDisksPerNodePerZone = this.replacementRootVolumes;
          params.rootDevicePerZone = this.replacementRootDevices;
          params.nodeNames =
              value.stream().map(NodeDetails::getNodeName).collect(Collectors.toList());

          log.info(
              "Creating {} root volumes using {} in AZ {} for nodes {}",
              params.numVolumes,
              params.getMachineImage(),
              node.cloudInfo.az,
              params.nodeNames);

          CreateRootVolumes task = createTask(CreateRootVolumes.class);
          task.initialize(params);
          subTaskGroup.addSubTask(task);
        });

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private ReplaceRootVolume createRootVolumeReplacementTask(
      NodeDetails node, Integer sshPortOverride) {
    ReplaceRootVolume.Params replaceParams = new ReplaceRootVolume.Params();
    replaceParams.nodeName = node.nodeName;
    replaceParams.azUuid = node.azUuid;
    replaceParams.setUniverseUUID(taskParams().getUniverseUUID());
    replaceParams.bootDisksPerNodePerZone = this.replacementRootVolumes;
    replaceParams.rootDevicePerZone = this.replacementRootDevices;
    replaceParams.sshPortOverride = sshPortOverride;

    ReplaceRootVolume replaceDiskTask = createTask(ReplaceRootVolume.class);
    replaceDiskTask.initialize(replaceParams);
    return replaceDiskTask;
  }

  private SubTaskGroup createCaptureFstabTask(Universe universe, NodeDetails node) {
    DeviceInfo deviceInfo =
        universe
            .getUniverseDetails()
            .getClusterByUuid(node.placementUuid)
            .userIntent
            .evaluateDeviceInfoForNode(node);
    SubTaskGroup subTaskGroup = createSubTaskGroup("CaptureFstab", getTaskSubGroupType());
    List<String> command = Arrays.asList("cat", "/etc/fstab");
    RunNodeCommand.Params params = new RunNodeCommand.Params();
    params.nodeName = node.nodeName;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.command = command;
    params.shellContext = ShellProcessContext.builder().logCmdOutput(true).build();
    params.responseConsumer =
        response -> {
          String output =
              response
                  .processErrors("Failed to read /etc/fstab on node " + node.nodeName)
                  .extractRunCommandOutput();
          Set<String> mountPoints = new HashSet<>(Util.getMountPoints(deviceInfo));
          Map<String, String> parsed = Util.parseFstabPathToUUID(output);
          parsed.keySet().retainAll(mountPoints);
          if (!parsed.keySet().equals(mountPoints)) {
            throw new PlatformServiceException(
                INTERNAL_SERVER_ERROR,
                "Expected to see mount points "
                    + mountPoints
                    + " on the node, whereas only found in fstab "
                    + parsed.keySet());
          }
          Map<String, String> mapping =
              deviceMappingByNode.computeIfAbsent(
                  node.getNodeUuid(), k -> new ConcurrentHashMap<>());
          mapping.clear();
          mapping.putAll(parsed);
          updateRuntimeInfo(
              RuntimeInfo.class,
              info -> info.deviceMappingByNode.put(node.getNodeUuid(), new HashMap<>(mapping)));
        };

    RunNodeCommand task = createTask(RunNodeCommand.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private String retreiveMachineImageForNode(NodeDetails node) {
    UUID clusterUuid = node.placementUuid;
    UniverseDefinitionTaskParams.Cluster cluster = getUniverse().getCluster(clusterUuid);
    UUID providerUUID = cluster.getProviderUUIDForNode(node);
    UUID imageBundleUUID = cluster.userIntent.getImageBundleUUIDForProvider(providerUUID);
    if (imageBundleUUID != null) {
      ImageBundle.NodeProperties imageBundleProperties =
          imageBundleUtil.getNodePropertiesOrFail(
              imageBundleUUID, node.getRegion(), node.cloudInfo.cloud);
      return imageBundleProperties.getMachineImage();
    }
    return null;
  }
}
