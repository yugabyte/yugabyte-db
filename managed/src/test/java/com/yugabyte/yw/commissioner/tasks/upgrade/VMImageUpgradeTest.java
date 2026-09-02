// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeast;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.DoCapacityReservation;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.nodeagent.ConfigureServiceOutput;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.ArgumentMatcher;
import org.mockito.InjectMocks;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class VMImageUpgradeTest extends UpgradeTaskTest {

  private static class CreateRootVolumesMatcher implements ArgumentMatcher<NodeTaskParams> {
    private final UUID azUUID;

    public CreateRootVolumesMatcher(UUID azUUID) {
      this.azUUID = azUUID;
    }

    @Override
    public boolean matches(NodeTaskParams right) {
      if (!(right instanceof CreateRootVolumes.Params)) {
        return false;
      }

      return right.azUuid.equals(this.azUUID);
    }
  }

  @InjectMocks private VMImageUpgrade vmImageUpgrade;

  private static final List<TaskType> UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.RunNodeCommand,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.ReplaceRootVolume,
          TaskType.UpdateUniverseFields,
          TaskType.SetupYNP,
          TaskType.YNPProvisioning,
          TaskType.InstallNodeAgent,
          TaskType.SetNodeStatus,
          TaskType.CheckLocale,
          TaskType.CheckGlibc,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitStartingFromTime,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.SetNodeState,
          TaskType.UpdateUniverseFields);

  private static final List<TaskType> NODE_VALIDATION_TASKS =
      ImmutableList.of(TaskType.CheckLocale, TaskType.CheckGlibc);

  @Override
  @Before
  public void setUp() {
    super.setUp();
    setCheckNodesAreSafeToTakeDown(mockClient);
    vmImageUpgrade.setUserTaskUUID(UUID.randomUUID());
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    mockLocaleCheckResponse(mockNodeUniverseManager);
    when(mockNodeUniverseManager.runCommand(
            any(), any(), eq(ImmutableList.of("cat", "/etc/fstab")), any()))
        .thenReturn(
            ShellResponse.create(
                0, ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "UUID=abc /mnt/d0 xfs defaults 0 0"));
  }

  private TaskInfo submitTask(VMImageUpgradeParams requestParams, int version) {
    return submitTask(requestParams, TaskType.VMImageUpgrade, commissioner, version);
  }

  @Test
  public void testVMImageUpgrade() {
    Region secondRegion = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(secondRegion, "az-4", "AZ 4", "subnet-4");

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster primaryCluster = universeDetails.getPrimaryCluster();
          UserIntent userIntent = primaryCluster.userIntent;
          userIntent.regionList = ImmutableList.of(region.getUuid(), secondRegion.getUuid());

          PlacementInfo placementInfo = primaryCluster.placementInfo;
          PlacementInfoUtil.addPlacementZone(az4.getUuid(), placementInfo, 1, 2, false);
          universe.setUniverseDetails(universeDetails);

          for (int idx = userIntent.numNodes + 1; idx <= userIntent.numNodes + 2; idx++) {
            NodeDetails node = new NodeDetails();
            node.nodeIdx = idx;
            node.placementUuid = primaryCluster.uuid;
            node.nodeName = "host-n" + idx;
            node.isMaster = true;
            node.isTserver = true;
            node.cloudInfo = new CloudSpecificInfo();
            node.cloudInfo.private_ip = "10.0.0." + idx;
            node.cloudInfo.cloud = "aws";
            node.cloudInfo.az = az4.getCode();
            node.azUuid = az4.getUuid();
            node.state = NodeDetails.NodeState.Live;
            universeDetails.nodeDetailsSet.add(node);
          }

          for (NodeDetails node : universeDetails.nodeDetailsSet) {
            node.nodeUuid = UUID.randomUUID();
          }

          userIntent.numNodes += 2;
          userIntent.providerType = CloudType.aws;
          userIntent.deviceInfo = new DeviceInfo();
          userIntent.deviceInfo.storageType = StorageType.Persistent;
          userIntent.deviceInfo.numVolumes = 1;
        };

    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    VMImageUpgradeParams taskParams = new VMImageUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.machineImages.put(region.getUuid(), "test-vm-image-1");
    taskParams.machineImages.put(secondRegion.getUuid(), "test-vm-image-2");

    // expect a CreateRootVolume for each AZ
    final int expectedRootVolumeCreationTasks = 4;

    Map<UUID, List<String>> createVolumeOutput =
        Stream.of(az1, az2, az3)
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        Collections.singletonList(String.format("root-volume-%s", az.getCode()))));
    // AZ 4 has 2 nodes so return 2 volumes here
    createVolumeOutput.put(az4.getUuid(), Arrays.asList("root-volume-4", "root-volume-5"));

    // Use output for verification and response is the raw string that parses into output.
    Map<UUID, String> createVolumeOutputResponse =
        Stream.of(az1, az2, az3)
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        String.format(
                            "{\"boot_disks_per_zone\":[\"root-volume-%s\"], "
                                + "\"root_device_name\":\"/dev/sda1\"}",
                            az.getCode())));
    createVolumeOutputResponse.put(
        az4.getUuid(),
        "{\"boot_disks_per_zone\":[\"root-volume-4\", \"root-volume-5\"], "
            + "\"root_device_name\":\"/dev/sda1\"}");

    for (Map.Entry<UUID, String> e : createVolumeOutputResponse.entrySet()) {
      when(mockNodeManager.nodeCommand(
              eq(NodeCommandType.Create_Root_Volumes),
              argThat(new CreateRootVolumesMatcher(e.getKey()))))
          .thenReturn(ShellResponse.create(0, e.getValue()));
    }

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckServiceLiveness);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodeCommandExecution);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateConsistencyCheck);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    List<TaskInfo> createRootVolumeTasks = subTasksByPosition.get(position++);
    assertTaskType(createRootVolumeTasks, TaskType.CreateRootVolumes);
    assertEquals(expectedRootVolumeCreationTasks, createRootVolumeTasks.size());

    /*
     * Leader blacklisting may add ModifyBlackList task to subTasks.
     * Task details for ModifyBlacklist task do not contain the required
     * keys being asserted here. So, remove task types of ModifyBlackList
     * from subTasks before asserting for required keys.
     */
    createRootVolumeTasks =
        createRootVolumeTasks.stream()
            .filter(t -> t.getTaskType() != TaskType.ModifyBlackList)
            .collect(Collectors.toList());
    createRootVolumeTasks.forEach(
        task -> {
          JsonNode details = task.getTaskParams();
          UUID azUuid = UUID.fromString(details.get("azUuid").asText());
          AvailabilityZone zone =
              AvailabilityZone.find.query().fetch("region").where().idEq(azUuid).findOne();
          String machineImage = details.get("machineImage").asText();
          assertEquals(taskParams.machineImages.get(zone.getRegion().getUuid()), machineImage);

          String azUUID = details.get("azUuid").asText();
          if (azUUID.equals(az4.getUuid().toString())) {
            assertEquals(2, details.get("numVolumes").asInt());
          }
        });

    List<Integer> nodeOrder = Arrays.asList(1, 3, 4, 5, 2);

    Map<UUID, Integer> replaceRootVolumeParams = new HashMap<>();

    for (int nodeIdx : nodeOrder) {
      String nodeName = String.format("host-n%d", nodeIdx);
      for (TaskType type : UPGRADE_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position++);

        assertEquals(1, tasks.size());

        TaskInfo task = tasks.get(0);
        TaskType taskType = task.getTaskType();

        assertEquals(type, taskType);

        if (!NON_NODE_TASKS.contains(taskType) && !NODE_VALIDATION_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

          assertNodeSubTask(tasks, assertValues);
        }

        if (taskType == TaskType.ReplaceRootVolume) {
          JsonNode details = task.getTaskParams();
          UUID az = UUID.fromString(details.get("azUuid").asText());
          replaceRootVolumeParams.compute(az, (k, v) -> v == null ? 1 : v + 1);
        }
      }
    }

    // Last task is DeleteRootVolumes.
    assertEquals(
        TaskType.UpdateUniverseFields, subTasksByPosition.get(position++).get(0).getTaskType());
    assertEquals(
        TaskType.DeleteRootVolumes, subTasksByPosition.get(position++).get(0).getTaskType());
    assertEquals(createVolumeOutput.keySet(), replaceRootVolumeParams.keySet());
    createVolumeOutput.forEach(
        (key, value) -> assertEquals(value.size(), (int) replaceRootVolumeParams.get(key)));
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

    // Captured fstab UUID mapping is stored in runtime info and passed into each YNP config.
    JsonNode deviceMappingByNode = taskInfo.getRuntimeInfo().get("deviceMappingByNode");
    assertNotNull(deviceMappingByNode);
    assertEquals(nodeOrder.size(), deviceMappingByNode.size());
    deviceMappingByNode
        .fields()
        .forEachRemaining(e -> assertEquals("abc", e.getValue().get("/mnt/d0").asText()));

    ArgumentCaptor<String> uploadedSourceCaptor = ArgumentCaptor.forClass(String.class);
    verify(mockNodeUniverseManager, atLeast(nodeOrder.size()))
        .uploadFileToNode(
            any(), any(), uploadedSourceCaptor.capture(), anyString(), anyString(), any());
    List<JsonNode> ynpConfigs =
        uploadedSourceCaptor.getAllValues().stream()
            .map(
                path -> {
                  try {
                    return Json.mapper().readTree(Files.readAllBytes(Paths.get(path)));
                  } catch (Exception e) {
                    return null;
                  }
                })
            .filter(node -> node != null && node.has("ynp") && node.has("extra"))
            .collect(Collectors.toList());
    assertEquals(nodeOrder.size(), ynpConfigs.size());
    for (JsonNode ynpConfig : ynpConfigs) {
      assertEquals("/mnt/d0=abc", ynpConfig.path("extra").path("path_to_uuid_mapping").asText());
    }
  }

  @Test
  public void testVMImageUpgradeWithImageBundle() {
    Region secondRegion = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(secondRegion, "az-4", "AZ 4", "subnet-4");

    ImageBundleDetails ibDetails = new ImageBundleDetails();
    ibDetails.setArch(Architecture.x86_64);
    ImageBundleDetails.BundleInfo bundleInfoRegion1 = new ImageBundleDetails.BundleInfo();
    Map<String, ImageBundleDetails.BundleInfo> ibRegionDetailsMap = new HashMap<>();

    bundleInfoRegion1.setYbImage("region-1-yb-image");
    bundleInfoRegion1.setSshUserOverride("region-1-ssh-user-override");

    ImageBundleDetails.BundleInfo bundleInfoRegion2 = new ImageBundleDetails.BundleInfo();
    bundleInfoRegion2.setYbImage("region-2-yb-image");
    bundleInfoRegion2.setSshUserOverride("region-2-ssh-user-override");

    ibRegionDetailsMap.put("region-1", bundleInfoRegion1);
    ibRegionDetailsMap.put("region-2", bundleInfoRegion2);
    ibDetails.setRegions(ibRegionDetailsMap);
    ImageBundle bundle = ImageBundle.create(defaultProvider, "ib-1", ibDetails, true);

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster primaryCluster = universeDetails.getPrimaryCluster();
          UserIntent userIntent = primaryCluster.userIntent;
          userIntent.regionList = ImmutableList.of(region.getUuid(), secondRegion.getUuid());

          PlacementInfo placementInfo = primaryCluster.placementInfo;
          PlacementInfoUtil.addPlacementZone(az4.getUuid(), placementInfo, 1, 2, false);
          universe.setUniverseDetails(universeDetails);

          for (NodeDetails node : universeDetails.nodeDetailsSet) {
            // Updating the region code in the node details.
            node.cloudInfo.region = "region-1";
            node.cloudInfo.cloud = Common.CloudType.aws.toString();
          }

          for (int idx = userIntent.numNodes + 1; idx <= userIntent.numNodes + 2; idx++) {
            NodeDetails node = new NodeDetails();
            node.nodeIdx = idx;
            node.placementUuid = primaryCluster.uuid;
            node.nodeName = "host-n" + idx;
            node.isMaster = true;
            node.isTserver = true;
            node.cloudInfo = new CloudSpecificInfo();
            node.cloudInfo.private_ip = "10.0.0." + idx;
            node.cloudInfo.cloud = "aws";
            node.cloudInfo.az = az4.getCode();
            node.cloudInfo.region = "region-2";
            node.cloudInfo.cloud = Common.CloudType.aws.toString();
            node.azUuid = az4.getUuid();
            node.state = NodeDetails.NodeState.Live;
            universeDetails.nodeDetailsSet.add(node);
          }

          for (NodeDetails node : universeDetails.nodeDetailsSet) {
            node.nodeUuid = UUID.randomUUID();
          }

          userIntent.numNodes += 2;
          userIntent.providerType = CloudType.aws;
          userIntent.deviceInfo = new DeviceInfo();
          userIntent.deviceInfo.storageType = StorageType.Persistent;
          userIntent.deviceInfo.numVolumes = 1;
        };

    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    VMImageUpgradeParams taskParams = new VMImageUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.machineImages = null;
    taskParams.imageBundleUUID = bundle.getUuid();

    // expect a CreateRootVolume for each AZ
    final int expectedRootVolumeCreationTasks = 4;

    Map<UUID, List<String>> createVolumeOutput =
        Stream.of(az1, az2, az3)
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        Collections.singletonList(String.format("root-volume-%s", az.getCode()))));
    // AZ 4 has 2 nodes so return 2 volumes here
    createVolumeOutput.put(az4.getUuid(), Arrays.asList("root-volume-4", "root-volume-5"));

    // Use output for verification and response is the raw string that parses into output.
    Map<UUID, String> createVolumeOutputResponse =
        Stream.of(az1, az2, az3)
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        String.format(
                            "{\"boot_disks_per_zone\":[\"root-volume-%s\"], "
                                + "\"root_device_name\":\"/dev/sda1\"}",
                            az.getCode())));
    createVolumeOutputResponse.put(
        az4.getUuid(),
        "{\"boot_disks_per_zone\":[\"root-volume-4\", \"root-volume-5\"], "
            + "\"root_device_name\":\"/dev/sda1\"}");

    for (Map.Entry<UUID, String> e : createVolumeOutputResponse.entrySet()) {
      when(mockNodeManager.nodeCommand(
              eq(NodeCommandType.Create_Root_Volumes),
              argThat(new CreateRootVolumesMatcher(e.getKey()))))
          .thenReturn(ShellResponse.create(0, e.getValue()));
    }

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckServiceLiveness);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodeCommandExecution);
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateConsistencyCheck);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    List<TaskInfo> createRootVolumeTasks = subTasksByPosition.get(position++);
    assertTaskType(createRootVolumeTasks, TaskType.CreateRootVolumes);
    assertEquals(expectedRootVolumeCreationTasks, createRootVolumeTasks.size());

    /*
     * Leader blacklisting may add ModifyBlackList task to subTasks.
     * Task details for ModifyBlacklist task do not contain the required
     * keys being asserted here. So, remove task types of ModifyBlackList
     * from subTasks before asserting for required keys.
     */
    createRootVolumeTasks =
        createRootVolumeTasks.stream()
            .filter(t -> t.getTaskType() != TaskType.ModifyBlackList)
            .collect(Collectors.toList());
    createRootVolumeTasks.forEach(
        task -> {
          JsonNode details = task.getTaskParams();
          UUID azUuid = UUID.fromString(details.get("azUuid").asText());
          AvailabilityZone zone =
              AvailabilityZone.find.query().fetch("region").where().idEq(azUuid).findOne();
          String machineImage = details.get("machineImage").asText();
          assertEquals(
              bundle.getDetails().getRegions().get(zone.getRegion().getCode()).getYbImage(),
              machineImage);

          String azUUID = details.get("azUuid").asText();
          if (azUUID.equals(az4.getUuid().toString())) {
            assertEquals(2, details.get("numVolumes").asInt());
          }
        });

    List<Integer> nodeOrder = Arrays.asList(1, 3, 4, 5, 2);

    Map<UUID, Integer> replaceRootVolumeParams = new HashMap<>();

    for (int nodeIdx : nodeOrder) {
      String nodeName = String.format("host-n%d", nodeIdx);
      for (TaskType type : UPGRADE_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position++);

        assertEquals(1, tasks.size());

        TaskInfo task = tasks.get(0);
        TaskType taskType = task.getTaskType();

        assertEquals(type, taskType);

        if (!NON_NODE_TASKS.contains(taskType) && !NODE_VALIDATION_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

          assertNodeSubTask(tasks, assertValues);
        }

        if (taskType == TaskType.ReplaceRootVolume) {
          JsonNode details = task.getTaskParams();
          UUID az = UUID.fromString(details.get("azUuid").asText());
          replaceRootVolumeParams.compute(az, (k, v) -> v == null ? 1 : v + 1);
        }

        if (taskType.equals(TaskType.AnsibleSetupServer)) {
          JsonNode details = task.getTaskParams();
          UUID azUuid = UUID.fromString(details.get("azUuid").asText());
          AvailabilityZone zone =
              AvailabilityZone.find.query().fetch("region").where().idEq(azUuid).findOne();
          String sshUser = "region-1-ssh-user-override";
          if (zone.getRegion().getCode().equals("region-2")) {
            sshUser = "region-2-ssh-user-override";
          }
          assertEquals(
              bundle.getDetails().getRegions().get(zone.getRegion().getCode()).getSshUserOverride(),
              sshUser);
        }
      }
    }

    assertEquals(
        TaskType.UpdateUniverseFields, subTasksByPosition.get(position++).get(0).getTaskType());
    assertEquals(
        TaskType.UpdateClusterUserIntent, subTasksByPosition.get(position++).get(0).getTaskType());
    // Last task is DeleteRootVolumes.
    assertEquals(
        TaskType.DeleteRootVolumes, subTasksByPosition.get(position++).get(0).getTaskType());
    assertEquals(createVolumeOutput.keySet(), replaceRootVolumeParams.keySet());
    createVolumeOutput.forEach(
        (key, value) -> assertEquals(value.size(), (int) replaceRootVolumeParams.get(key)));
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testVMImageUpgradeRetries() {
    Region secondRegion = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(secondRegion, "az-4", "AZ 4", "subnet-4");

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster primaryCluster = universeDetails.getPrimaryCluster();
          UserIntent userIntent = primaryCluster.userIntent;
          userIntent.regionList = ImmutableList.of(region.getUuid(), secondRegion.getUuid());

          PlacementInfo placementInfo = primaryCluster.placementInfo;
          PlacementInfoUtil.addPlacementZone(az4.getUuid(), placementInfo, 1, 2, false);
          universe.setUniverseDetails(universeDetails);

          for (int idx = userIntent.numNodes + 1; idx <= userIntent.numNodes + 2; idx++) {
            NodeDetails node = new NodeDetails();
            node.nodeIdx = idx;
            node.placementUuid = primaryCluster.uuid;
            node.nodeName = "host-n" + idx;
            node.isMaster = true;
            node.isTserver = true;
            node.cloudInfo = new CloudSpecificInfo();
            node.cloudInfo.private_ip = "10.0.0." + idx;
            node.cloudInfo.cloud = "aws";
            node.cloudInfo.az = az4.getCode();
            node.azUuid = az4.getUuid();
            node.state = NodeDetails.NodeState.Live;
            universeDetails.nodeDetailsSet.add(node);
          }

          for (NodeDetails node : universeDetails.nodeDetailsSet) {
            node.nodeUuid = UUID.randomUUID();
          }

          userIntent.numNodes += 2;
          userIntent.providerType = CloudType.aws;
          userIntent.deviceInfo = new DeviceInfo();
          userIntent.deviceInfo.storageType = StorageType.Persistent;
          userIntent.deviceInfo.numVolumes = 1;
        };

    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    VMImageUpgradeParams taskParams = new VMImageUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.machineImages.put(region.getUuid(), "test-vm-image-1");
    taskParams.machineImages.put(secondRegion.getUuid(), "test-vm-image-2");
    taskParams.creatingUser = defaultUser;
    taskParams.expectedUniverseVersion = -1;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    Map<UUID, List<String>> createVolumeOutput =
        Stream.of(az1, az2, az3)
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        Collections.singletonList(String.format("root-volume-%s", az.getCode()))));
    // AZ 4 has 2 nodes so return 2 volumes here
    createVolumeOutput.put(az4.getUuid(), Arrays.asList("root-volume-4", "root-volume-5"));

    // Use output for verification and response is the raw string that parses into output.
    Map<UUID, String> createVolumeOutputResponse =
        Stream.of(az1, az2, az3)
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        String.format(
                            "{\"boot_disks_per_zone\":[\"root-volume-%s\"], "
                                + "\"root_device_name\":\"/dev/sda1\"}",
                            az.getCode())));
    createVolumeOutputResponse.put(
        az4.getUuid(),
        "{\"boot_disks_per_zone\":[\"root-volume-4\", \"root-volume-5\"], "
            + "\"root_device_name\":\"/dev/sda1\"}");

    for (Map.Entry<UUID, String> e : createVolumeOutputResponse.entrySet()) {
      when(mockNodeManager.nodeCommand(
              eq(NodeCommandType.Create_Root_Volumes),
              argThat(new CreateRootVolumesMatcher(e.getKey()))))
          .thenReturn(ShellResponse.create(0, e.getValue()));
    }

    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.VMImageUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.VMImageUpgrade,
        taskParams,
        false);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }

  @Test
  public void testVMImageUpgradeEnableEarlyoom() {
    ImageBundleDetails ibDetails = new ImageBundleDetails();
    ibDetails.setArch(Architecture.x86_64);
    ImageBundleDetails.BundleInfo bundleInfoRegion1 = new ImageBundleDetails.BundleInfo();
    Map<String, ImageBundleDetails.BundleInfo> ibRegionDetailsMap = new HashMap<>();
    RuntimeConfigEntry.upsertGlobal(CustomerConfKeys.enableEarlyoomFeature.getKey(), "true");
    RuntimeConfigEntry.upsertGlobal(
        ProviderConfKeys.enableEarlyoomByDefaultForProvider.getKey(), "true");
    RuntimeConfigEntry.upsertGlobal(ProviderConfKeys.enableEarlyoomOnOSUpgrade.getKey(), "true");
    when(mockNodeUniverseManager.maybeUpgradeAndGetNodeAgent(any(), any()))
        .thenReturn(Optional.of(new NodeAgent()));
    when(mockNodeAgentClient.runConfigureEarlyoom(any(), any(), anyString()))
        .thenReturn(ConfigureServiceOutput.newBuilder().setSuccess(true).build());

    bundleInfoRegion1.setYbImage("region-1-yb-image");
    bundleInfoRegion1.setSshUserOverride("region-1-ssh-user-override");
    ibRegionDetailsMap.put("region-1", bundleInfoRegion1);
    ibDetails.setRegions(ibRegionDetailsMap);
    ImageBundle bundle = ImageBundle.create(defaultProvider, "ib-1", ibDetails, true);

    VMImageUpgradeParams taskParams = new VMImageUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.machineImages = null;
    taskParams.imageBundleUUID = bundle.getUuid();

    // Use output for verification and response is the raw string that parses into output.
    Map<UUID, String> createVolumeOutputResponse =
        Stream.of(az1, az2, az3)
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        String.format(
                            "{\"boot_disks_per_zone\":[\"root-volume-%s\"], "
                                + "\"root_device_name\":\"/dev/sda1\"}",
                            az.getCode())));

    for (Map.Entry<UUID, String> e : createVolumeOutputResponse.entrySet()) {
      when(mockNodeManager.nodeCommand(
              eq(NodeCommandType.Create_Root_Volumes),
              argThat(new CreateRootVolumesMatcher(e.getKey()))))
          .thenReturn(ShellResponse.create(0, e.getValue()));
    }

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    long configureOOMTasks =
        subTasks.stream()
            .filter(t -> t.getTaskType() == TaskType.ConfigureOOMServiceOnNode)
            .count();
    assertEquals(defaultUniverse.getNodes().size(), configureOOMTasks);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertNotNull(defaultUniverse.getUniverseDetails().additionalServicesStateData);
    assertTrue(
        defaultUniverse.getUniverseDetails().additionalServicesStateData.isEarlyoomEnabled());
  }

  @Test
  public void testVMImageUpgradeWithCapacityReservationAws() {
    factory
        .globalRuntimeConf()
        .setValue(ProviderConfKeys.enableCapacityReservationAws.getKey(), "true");
    String instanceType = ApiUtils.UTIL_INST_TYPE;
    Region crRegion = prepareOsUpgradeUniverse(defaultProvider, instanceType);
    TaskInfo taskInfo = submitOsUpgradeWithCapacityReservation(crRegion);
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.DoCapacityReservation));
    assertTrue(
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.DeleteCapacityReservation));

    List<String> nodeNames = nodeNamesInAzOrder();
    verifyCapacityReservationAws(
        defaultUniverse.getUniverseUUID(),
        Map.of(instanceType, Map.of("1", new ZoneData("region-1", nodeNames))));
    verifyReplaceRootVolumeReservations(
        Map.of(
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                "az-1",
                instanceType),
            nodeNames));
  }

  @Test
  public void testVMImageUpgradeWithCapacityReservationAzure() {
    factory
        .globalRuntimeConf()
        .setValue(ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");
    String instanceType = "Standard_D4as_v4";
    Region crRegion = prepareOsUpgradeUniverse(azuProvider, instanceType);
    TaskInfo taskInfo = submitOsUpgradeWithCapacityReservation(crRegion);
    assertEquals(Success, taskInfo.getTaskState());

    List<String> nodeNames = nodeNamesInAzOrder();
    verifyCapacityReservationAZU(
        defaultUniverse.getUniverseUUID(),
        AzureReservationGroup.of(crRegion, Map.of(instanceType, Map.of("1", nodeNames))));
    verifyReplaceRootVolumeReservations(
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                crRegion.getCode()),
            nodeNames));
  }

  @Test
  public void testVMImageUpgradeWithCapacityReservationGcp() throws Exception {
    factory
        .globalRuntimeConf()
        .setValue(ProviderConfKeys.enableCapacityReservationGcp.getKey(), "true");
    String instanceType = "n2-standard-4";
    Region crRegion = prepareOsUpgradeUniverse(gcpProvider, instanceType);
    TaskInfo taskInfo = submitOsUpgradeWithCapacityReservation(crRegion);
    assertEquals(Success, taskInfo.getTaskState());

    List<String> nodeNames = nodeNamesInAzOrder();
    verifyCapacityReservationGcp(
        defaultUniverse.getUniverseUUID(),
        Map.of(instanceType, Map.of("1", new ZoneData("region-1", nodeNames))));

    ArgumentCaptor<String> nameCaptor = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> zoneCaptor = ArgumentCaptor.forClass(String.class);
    Mockito.verify(gcpProjectApiClient, Mockito.atLeast(0))
        .createCapacityReservation(
            nameCaptor.capture(),
            zoneCaptor.capture(),
            Mockito.anyString(),
            Mockito.anyInt(),
            Mockito.anyMap());
    Map<String, String> zoneToName = new HashMap<>();
    for (int idx = 0; idx < nameCaptor.getAllValues().size(); idx++) {
      zoneToName.put(zoneCaptor.getAllValues().get(idx), nameCaptor.getAllValues().get(idx));
    }
    verifyReplaceRootVolumeReservations(Map.of(zoneToName.get("az-1"), nodeNames));
  }

  private Region prepareOsUpgradeUniverse(Provider provider, String instanceType) {
    Region crRegion;
    AvailabilityZone crZone;
    if (provider.getUuid().equals(defaultProvider.getUuid())) {
      crRegion = region;
      crZone = az1;
    } else {
      crRegion = Region.create(provider, "region-1", "region-1", "img");
      crZone = AvailabilityZone.createOrThrow(crRegion, "az-1", "az 1", "subn");
    }
    InstanceType instanceTypeRecord =
        InstanceType.upsert(
            provider.getUuid(), instanceType, 10, 5.5, new InstanceType.InstanceTypeDetails());
    UserIntent userIntent = getTestUserIntent(crRegion, provider, instanceTypeRecord, 3);
    userIntent.universeName = "universe-test";
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "2.21.1.1-b1";
    userIntent.accessKeyCode = "demo-access";
    userIntent.deviceInfo.storageType = StorageType.Persistent;

    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(crZone.getUuid(), placementInfo, 3, 3, true);

    defaultUniverse =
        ModelFactory.createUniverse(
            "universe-test", defaultCustomer.getId(), provider.getCloudCode());
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(
                userIntent, "universe-test", true /* setMasters */, false, placementInfo));
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe ->
                universe
                    .getUniverseDetails()
                    .nodeDetailsSet
                    .forEach(
                        node -> {
                          node.cloudInfo.cloud = provider.getCode();
                          node.cloudInfo.instance_type = instanceType;
                          node.nodeUuid = UUID.randomUUID();
                        }));
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    return crRegion;
  }

  private TaskInfo submitOsUpgradeWithCapacityReservation(Region crRegion) {
    Map<UUID, List<String>> volumesByAz = new HashMap<>();
    defaultUniverse
        .getNodes()
        .forEach(
            node ->
                volumesByAz
                    .computeIfAbsent(node.azUuid, x -> new ArrayList<>())
                    .add("root-volume-" + node.nodeName));
    volumesByAz.forEach(
        (azUuid, volumes) -> {
          String bootDisks =
              volumes.stream().map(v -> "\"" + v + "\"").collect(Collectors.joining(", "));
          when(mockNodeManager.nodeCommand(
                  eq(NodeCommandType.Create_Root_Volumes),
                  argThat(new CreateRootVolumesMatcher(azUuid))))
              .thenReturn(
                  ShellResponse.create(
                      0,
                      "{\"boot_disks_per_zone\":["
                          + bootDisks
                          + "], \"root_device_name\":\"/dev/sda1\"}"));
        });

    VMImageUpgradeParams taskParams = new VMImageUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.machineImages.put(crRegion.getUuid(), "test-vm-image");
    return submitTask(taskParams, defaultUniverse.getVersion());
  }

  private List<String> nodeNamesInAzOrder() {
    return defaultUniverse.getNodes().stream()
        .map(n -> n.nodeName)
        .sorted()
        .collect(Collectors.toList());
  }

  private void verifyReplaceRootVolumeReservations(Map<String, List<String>> reservationToNodes) {
    int nodeCommandCount =
        (int)
            Mockito.mockingDetails(mockNodeManager).getInvocations().stream()
                .filter(inv -> inv.getMethod().getName().equals("nodeCommand"))
                .count();
    verifyNodeInteractionsCapacityReservation(
        nodeCommandCount,
        NodeManager.NodeCommandType.Replace_Root_Volume,
        params -> ((ReplaceRootVolume.Params) params).capacityReservation,
        reservationToNodes);
  }
}
