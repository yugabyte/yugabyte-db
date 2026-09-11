// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeast;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.tasks.local.LocalProviderUniverseTestBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.BiConsumer;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.ArgumentMatcher;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListMasterRaftPeersResponse;
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

  @Override
  @Before
  public void setUp() {
    super.setUp();
    setCheckNodesAreSafeToTakeDown(mockClient);
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    vmImageUpgrade.setUserTaskUUID(UUID.randomUUID());
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    // Empty peers: a master remove is already done, so ChangeMasterConfig returns before the RPC.
    try {
      ListMasterRaftPeersResponse listMastersResponse = mock(ListMasterRaftPeersResponse.class);
      when(listMastersResponse.getPeersList()).thenReturn(Collections.emptyList());
      when(mockClient.listMasterRaftPeers()).thenReturn(listMastersResponse);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
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
    if (taskInfo.getTaskState() == TaskInfo.State.Failure) {
      throw new IllegalStateException(
          "Task failed " + LocalProviderUniverseTestBase.getAllErrorsStr(taskInfo));
    }
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);

    List<JsonNode> createRootVolumeParams =
        Arrays.asList(
            Json.newObject()
                .put("numVolumes", "1")
                .put("machineImage", taskParams.machineImages.get(region.getUuid())),
            Json.newObject()
                .put("numVolumes", "1")
                .put("machineImage", taskParams.machineImages.get(region.getUuid())),
            Json.newObject()
                .put("numVolumes", "1")
                .put("machineImage", taskParams.machineImages.get(region.getUuid())),
            Json.newObject()
                .put("numVolumes", "2")
                .put("machineImage", taskParams.machineImages.get(secondRegion.getUuid())));
    List<Integer> nodeOrder = Arrays.asList(1, 3, 4, 5, 2);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addSimultaneousTasks(
            TaskType.CreateRootVolumes, createRootVolumeParams.toArray(new JsonNode[0]))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(getUpgradeContext(mockUpgrade))
        .tserverTasks(
            TaskType.ReplaceRootVolume,
            TaskType.UpdateUniverseFields,
            TaskType.SetupYNP,
            TaskType.YNPProvisioning,
            TaskType.InstallNodeAgent,
            TaskType.SetNodeStatus,
            TaskType.CheckLocale,
            TaskType.CheckGlibc,
            TaskType.AnsibleConfigureServers,
            TaskType.AnsibleClusterServerCtl, // Stop tserver to be safe.
            TaskType.AnsibleConfigureServers // Gflags upgrade for tserver
            )
        .masterTasks(
            TaskType.AnsibleClusterServerCtl, // Stop master to be safe.
            TaskType.AnsibleConfigureServers) // Gflags upgrade for master
        .applyRound()
        .addSimultaneousTasks(TaskType.DeleteRootVolumes, nodeOrder.size())
        .addTask(TaskType.MarkUniverseForHealthScriptReUpload, null)
        .verifyTasks(taskInfo.getSubTasks());

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
    if (taskInfo.getTaskState() == TaskInfo.State.Failure) {
      throw new IllegalStateException(
          "Task failed " + LocalProviderUniverseTestBase.getAllErrorsStr(taskInfo));
    }
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);

    List<JsonNode> createRootVolumeParams =
        Arrays.asList(
            Json.newObject()
                .put("numVolumes", "1")
                .put("machineImage", ibDetails.getRegions().get("region-1").getYbImage()),
            Json.newObject()
                .put("numVolumes", "1")
                .put("machineImage", ibDetails.getRegions().get("region-1").getYbImage()),
            Json.newObject()
                .put("numVolumes", "1")
                .put("machineImage", ibDetails.getRegions().get("region-1").getYbImage()),
            Json.newObject()
                .put("numVolumes", "2")
                .put("machineImage", ibDetails.getRegions().get("region-2").getYbImage()));

    BiConsumer<JsonNode, NodeDetails> sshUserCustomizer =
        (jsonNode, nodeDetails) -> {
          AvailabilityZone zone =
              AvailabilityZone.find
                  .query()
                  .fetch("region")
                  .where()
                  .idEq(nodeDetails.azUuid)
                  .findOne();
          String sshUser = "region-1-ssh-user-override";
          if (zone.getRegion().getCode().equals("region-2")) {
            sshUser = "region-2-ssh-user-override";
          }
          ((ObjectNode) jsonNode).put("sshUser", sshUser);
        };
    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addSimultaneousTasks(
            TaskType.CreateRootVolumes, createRootVolumeParams.toArray(new JsonNode[0]))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(getUpgradeContext(mockUpgrade))
        .tserverTasks(TaskType.ReplaceRootVolume, TaskType.UpdateUniverseFields)
        // Verifying that these tasks have appropriate sshUser
        .tserverTask(TaskType.SetupYNP, Json.newObject(), sshUserCustomizer)
        .tserverTask(TaskType.YNPProvisioning, Json.newObject(), sshUserCustomizer)
        .tserverTasks(
            TaskType.InstallNodeAgent,
            TaskType.SetNodeStatus,
            TaskType.CheckLocale,
            TaskType.CheckGlibc,
            TaskType.AnsibleConfigureServers,
            TaskType.AnsibleClusterServerCtl, // Stop tserver to be safe.
            TaskType.AnsibleConfigureServers // Gflags upgrade for tserver
            )
        .masterTasks(
            TaskType.AnsibleClusterServerCtl, // Stop master to be safe.
            TaskType.AnsibleConfigureServers) // Gflags upgrade for master
        .applyRound()
        .addTask(TaskType.UpdateClusterUserIntent, null)
        .addSimultaneousTasks(TaskType.DeleteRootVolumes, defaultUniverse.getNodes().size())
        .addTask(TaskType.MarkUniverseForHealthScriptReUpload, null)
        .verifyTasks(taskInfo.getSubTasks());
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

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(VMImageUpgrade.class);
  }

  private UpgradeTaskBase.UpgradeContext getUpgradeContext(MockUpgrade mockUpgrade) {
    return UpgradeTaskBase.UpgradeContext.builder()
        .runBeforeStopping(false)
        .processInactiveMaster(false)
        .reconfigureMaster(true)
        .nodesAreStopped(true)
        .preAction(
            node -> {
              mockUpgrade.addTask(TaskType.RunNodeCommand, null); // Capture fstab.
            })
        .postAction(
            node -> {
              mockUpgrade.addTask(TaskType.UpdateUniverseFields, null);
            })
        .build();
  }
}
