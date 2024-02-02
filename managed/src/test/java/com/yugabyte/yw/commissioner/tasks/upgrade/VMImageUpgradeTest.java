// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
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
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
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
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentMatcher;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

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
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.ReplaceRootVolume,
          TaskType.AnsibleSetupServer,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.UpdateNodeDetails);

  @Override
  @Before
  public void setUp() {
    super.setUp();

    vmImageUpgrade.setUserTaskUUID(UUID.randomUUID());
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
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
          JsonNode details = task.getDetails();
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

        if (!NON_NODE_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

          assertNodeSubTask(tasks, assertValues);
        }

        if (taskType == TaskType.ReplaceRootVolume) {
          JsonNode details = task.getDetails();
          UUID az = UUID.fromString(details.get("azUuid").asText());
          replaceRootVolumeParams.compute(az, (k, v) -> v == null ? 1 : v + 1);
        }
      }
    }

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
          JsonNode details = task.getDetails();
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

        if (!NON_NODE_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

          assertNodeSubTask(tasks, assertValues);
        }

        if (taskType == TaskType.ReplaceRootVolume) {
          JsonNode details = task.getDetails();
          UUID az = UUID.fromString(details.get("azUuid").asText());
          replaceRootVolumeParams.compute(az, (k, v) -> v == null ? 1 : v + 1);
        }

        if (taskType.equals(TaskType.AnsibleSetupServer)) {
          JsonNode details = task.getDetails();
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
        };

    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    VMImageUpgradeParams taskParams = new VMImageUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.machineImages.put(region.getUuid(), "test-vm-image-1");
    taskParams.machineImages.put(secondRegion.getUuid(), "test-vm-image-2");
    taskParams.creatingUser = defaultUser;
    taskParams.expectedUniverseVersion = -1;
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
  }
}
