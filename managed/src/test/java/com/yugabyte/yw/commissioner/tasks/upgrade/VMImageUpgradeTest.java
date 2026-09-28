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
import com.yugabyte.yw.forms.UpgradeTaskParams;
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
import java.util.function.BiConsumer;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.jetbrains.annotations.NotNull;
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

  @Override
  @Before
  public void setUp() {
    super.setUp();
    setCheckNodesAreSafeToTakeDown(mockClient);
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    vmImageUpgrade.setUserTaskUUID(UUID.randomUUID());
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
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
        .addTask(TaskType.UpdateUniverseFields, null)
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

    ImageBundleDetails ibDetails = getImageBundleDetails(1, 2);
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
        .addTask(TaskType.UpdateUniverseFields, null)
        .addTask(TaskType.UpdateClusterUserIntent, null)
        .addSimultaneousTasks(TaskType.DeleteRootVolumes, defaultUniverse.getNodes().size())
        .addTask(TaskType.MarkUniverseForHealthScriptReUpload, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testVMImageUpgradeMultiprovider() {
    ImageBundle bundle =
        ImageBundle.create(defaultProvider, "ib-1", getImageBundleDetails(1), true);

    Region secondRegion = Region.create(azuProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(secondRegion, "az-4", "AZ 4", "subnet-4");

    // Non-AWS providers use globalYbImage (not per-region images).
    ImageBundleDetails azuIbDetails = new ImageBundleDetails();
    azuIbDetails.setArch(Architecture.x86_64);
    azuIbDetails.setGlobalYbImage("region-2-yb-image");
    azuIbDetails.setSshUser("region-2-ssh-user-override");
    ImageBundle bundle2 = ImageBundle.create(azuProvider, "ib-2", azuIbDetails, true);

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster primaryCluster = universeDetails.getPrimaryCluster();
          UserIntent userIntent = primaryCluster.userIntent;
          userIntent.providerType = CloudType.aws;
          userIntent.deviceInfo = new DeviceInfo();
          userIntent.deviceInfo.storageType = StorageType.Persistent;
          userIntent.deviceInfo.numVolumes = 1;

          UniverseDefinitionTaskParams.ProviderSpecification azuSpec =
              new UniverseDefinitionTaskParams.ProviderSpecification();
          azuSpec.setProviderUUID(azuProvider.getUuid());
          azuSpec.setProviderType(CloudType.azu);
          DeviceInfo deviceInfo = new DeviceInfo();
          deviceInfo.storageType = StorageType.Premium_LRS;
          deviceInfo.volumeSize = 100;
          deviceInfo.numVolumes = 1;
          azuSpec.setNodesSpecs(TestUtils.tserverSpec("azuInstanceType", deviceInfo));

          userIntent.providerSpecifications =
              Arrays.asList(TestUtils.toProviderSpecification(userIntent), azuSpec);

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
            node.cloudInfo.az = az4.getCode();
            node.cloudInfo.region = "region-2";
            node.cloudInfo.cloud = CloudType.azu.toString();
            node.azUuid = az4.getUuid();
            node.state = NodeDetails.NodeState.Live;
            universeDetails.nodeDetailsSet.add(node);
          }

          for (NodeDetails node : universeDetails.nodeDetailsSet) {
            node.nodeUuid = UUID.randomUUID();
          }

          userIntent.numNodes += 2;
        };

    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    VMImageUpgradeParams taskParams = new VMImageUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.machineImages = null;
    taskParams.imageBundles =
        Arrays.asList(
            new VMImageUpgradeParams.ImageBundleUpgradeInfo(),
            new VMImageUpgradeParams.ImageBundleUpgradeInfo());
    taskParams.imageBundles.get(0).setClusterUuid(taskParams.clusters.get(0).uuid);
    taskParams.imageBundles.get(0).setProviderUuid(defaultProvider.getUuid());
    taskParams.imageBundles.get(0).setImageBundleUuid(bundle.getUuid());

    taskParams.imageBundles.get(1).setClusterUuid(taskParams.clusters.get(0).uuid);
    taskParams.imageBundles.get(1).setProviderUuid(azuProvider.getUuid());
    taskParams.imageBundles.get(1).setImageBundleUuid(bundle2.getUuid());

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

    String ybImage = bundle.getDetails().getRegions().get(region.getCode()).getYbImage();
    List<JsonNode> createRootVolumeParams =
        Arrays.asList(
            Json.newObject().put("numVolumes", "1").put("machineImage", ybImage),
            Json.newObject().put("numVolumes", "1").put("machineImage", ybImage),
            Json.newObject().put("numVolumes", "1").put("machineImage", ybImage),
            Json.newObject()
                .put("numVolumes", "2")
                // for azure we use global image.
                .put("machineImage", bundle2.getDetails().getGlobalYbImage()));

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
        .addTask(TaskType.UpdateUniverseFields, null)
        .addTask(TaskType.UpdateClusterUserIntent, null)
        .addSimultaneousTasks(TaskType.DeleteRootVolumes, defaultUniverse.getNodes().size())
        .addTask(TaskType.MarkUniverseForHealthScriptReUpload, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @NotNull
  private static ImageBundleDetails getImageBundleDetails(Integer... regionIds) {
    ImageBundleDetails ibDetails = new ImageBundleDetails();
    ibDetails.setArch(Architecture.x86_64);
    Map<String, ImageBundleDetails.BundleInfo> ibRegionDetailsMap = new HashMap<>();

    for (Integer regionId : regionIds) {
      ImageBundleDetails.BundleInfo bundleInfoRegion1 = new ImageBundleDetails.BundleInfo();

      bundleInfoRegion1.setYbImage("region-" + regionId + "-yb-image");
      bundleInfoRegion1.setSshUserOverride("region-" + regionId + "-ssh-user-override");
      ibRegionDetailsMap.put("region-" + regionId, bundleInfoRegion1);
    }
    ibDetails.setRegions(ibRegionDetailsMap);
    return ibDetails;
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
