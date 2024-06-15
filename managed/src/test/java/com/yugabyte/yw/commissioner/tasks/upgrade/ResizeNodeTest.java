// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.time.DateUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.ListMastersResponse;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class ResizeNodeTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static final String DEFAULT_INSTANCE_TYPE = "c3.medium";
  private static final String NEW_INSTANCE_TYPE = "c4.medium";
  private static final String NEW_READ_ONLY_INSTANCE_TYPE = "c3.small";

  private static final int DEFAULT_VOLUME_SIZE = 100;
  private static final int NEW_VOLUME_SIZE = 200;
  private static final int DEFAULT_DISK_IOPS = 3000;
  private static final int NEW_DISK_IOPS = 5000;
  private static final int DEFAULT_DISK_THROUGHPUT = 125;
  private static final int NEW_DISK_THROUGHPUT = 250;
  private static final int NEW_CGROUP_SIZE = 10;

  @InjectMocks private ResizeNode resizeNode;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    resizeNode.setUserTaskUUID(UUID.randomUUID());
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.deviceInfo = new DeviceInfo();
              userIntent.deviceInfo.numVolumes = 1;
              userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
              userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
              userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
              userIntent.provider = defaultProvider.getUuid().toString();
              universe
                  .getNodes()
                  .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE);
            });
    try {
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
      setCheckNodesAreSafeToTakeDown(mockClient);
    } catch (Exception ignored) {
    }

    createInstanceType(defaultProvider.getUuid(), DEFAULT_INSTANCE_TYPE);
    createInstanceType(defaultProvider.getUuid(), NEW_INSTANCE_TYPE);
    createInstanceType(defaultProvider.getUuid(), NEW_READ_ONLY_INSTANCE_TYPE);

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    setLeaderlessTabletsMock();
  }

  @Override
  protected PlacementInfo createPlacementInfo() {
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), placementInfo, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), placementInfo, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), placementInfo, 1, 2, false);
    return placementInfo;
  }

  @Parameters({
    "aws, 0, 10, m3.medium, m3.medium, true",
    "gcp, 0, 10, m3.medium, m3.medium, true",
    "aws, 0, 10, m3.medium, c4.medium, true",
    "aws, 0, -10, m3.medium, m3.medium, false", // decrease volume
    "aws, 1, 10, m3.medium, m3.medium, false", // change num of volumes
    "aws, 0, 10, m3.medium, fake_type, false", // unknown instance type
    "aws, 0, 10, i3.instance, m3.medium, false", // ephemeral instance type
    "aws, 0, 10, c5d.instance, m3.medium, false", // ephemeral instance type
    "gcp, 0, 10, scratch, m3.medium, false", // ephemeral instance type
    "aws, 0, 10, m3.medium, c5d.instance, true" // changing to ephemeral is OK
  })
  @Test
  public void testResizeNodeAvailable(
      String cloudTypeStr,
      int numOfVolumesDiff,
      int volumeSizeDiff,
      String curInstanceTypeCode,
      String targetInstanceTypeCode,
      boolean expected) {
    testResizeNodeAvailable(
        cloudTypeStr,
        numOfVolumesDiff,
        volumeSizeDiff,
        curInstanceTypeCode,
        targetInstanceTypeCode,
        false /* volumeIopsChange */,
        false /* volumeThroughputChange */,
        null /* storageType */,
        expected);
  }

  @Parameters({"aws, GP3, true", "aws, GP2, false", "gcp, null, false", "azu, null, false"})
  @Test
  public void testResizeNodeIopsThroughputAvailable(
      String cloudTypeStr, @Nullable String storageTypeStr, boolean expected) {
    testResizeNodeAvailable(
        cloudTypeStr,
        0,
        10,
        "m3.medium",
        "m3.medium",
        true /* volumeIopsChange */,
        true /* volumeThroughputChange */,
        storageTypeStr != null ? PublicCloudConstants.StorageType.valueOf(storageTypeStr) : null,
        expected);
  }

  @Parameters({
    "10, Standard_DS2_v2, Standard_DS2_v2, UltraSSD_LRS, false",
    "0, Standard_DS2_v2, Standard_DS4_v2, UltraSSD_LRS, true",
    "10, Standard_DS2_v2, Standard_DS2_v2, StandardSSD_LRS, true",
    "10, Standard_DS2_v2, Standard_DS4_v2, StandardSSD_LRS, true",
    "10, Standard_DS2_v2, Standard_E2as_v5, StandardSSD_LRS, false", // local to no local
    "10, Standard_E2as_v5, Standard_D32as_v5, StandardSSD_LRS, true", // no local to no local
    "10, Standard_D32as_v5, Standard_DS2_v5, StandardSSD_LRS, false", // no local to local
    "5000, Standard_DS2_v2, Standard_DS2_v2, StandardSSD_LRS, false",
  })
  @Test
  public void testResizeNodeAzu(
      int volumeSizeDiff,
      String curInstanceTypeCode,
      String targetInstanceTypeCode,
      String volumeType,
      boolean expected) {
    testResizeNodeAvailable(
        Common.CloudType.azu.toString(),
        0,
        volumeSizeDiff,
        curInstanceTypeCode,
        targetInstanceTypeCode,
        false /* volumeIopsChange */,
        false /* volumeThroughputChange */,
        PublicCloudConstants.StorageType.valueOf(volumeType),
        expected);
  }

  private void testResizeNodeAvailable(
      String cloudTypeStr,
      int numOfVolumesDiff,
      int volumeSizeDiff,
      String curInstanceTypeCode,
      String targetInstanceTypeCode,
      boolean volumeIopsChange,
      boolean volumeThroughputChange,
      PublicCloudConstants.StorageType storageType,
      boolean expected) {
    Common.CloudType cloudType = Common.CloudType.valueOf(cloudTypeStr);
    if (storageType == null) {
      storageType = chooseStorageType(cloudType, curInstanceTypeCode.equals("scratch"));
    }
    UniverseDefinitionTaskParams.UserIntent currentIntent =
        createIntent(cloudType, curInstanceTypeCode, storageType);

    UniverseDefinitionTaskParams.UserIntent targetIntent =
        createIntent(cloudType, targetInstanceTypeCode, storageType);
    targetIntent.deviceInfo.volumeSize += volumeSizeDiff;
    targetIntent.deviceInfo.numVolumes += numOfVolumesDiff;
    if (volumeIopsChange) {
      targetIntent.deviceInfo.diskIops = NEW_DISK_IOPS;
    }
    if (volumeThroughputChange) {
      targetIntent.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    }

    createInstanceType(UUID.fromString(currentIntent.provider), curInstanceTypeCode);
    createInstanceType(UUID.fromString(currentIntent.provider), targetInstanceTypeCode);
    assertEquals(
        expected,
        ResizeNodeParams.checkResizeIsPossible(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid,
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
  }

  @Test
  public void testAwsBackToBackResizeNode() {
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        defaultUniverse.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent targetIntent = primaryCluster.userIntent.clone();
    targetIntent.deviceInfo.volumeSize += 1;
    UniverseDefinitionTaskParams.UserIntent targetIntentJustType =
        primaryCluster.userIntent.clone();
    targetIntentJustType.instanceType = NEW_INSTANCE_TYPE;
    UUID primaryUUID = primaryCluster.uuid;
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
    RuntimeConfigEntry.upsertGlobal("yb.aws.disk_resize_cooldown_hours", "3");
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              Date date = DateUtils.addHours(new Date(), -2);
              univ.getNodes().forEach(node -> node.lastVolumeUpdateTime = date);
            });
    assertFalse(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
    // Just changing instance type is available, even within cooldown window.
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntentJustType,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
    // Changing window size.
    RuntimeConfigEntry.upsertGlobal("yb.aws.disk_resize_cooldown_hours", "1");
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
  }

  /*
   Instance type codes:
   m -> m3.medium
   i -> i3.instance (ephemeral)
   c -> c4.medium
   Device codes:
   10 -> device with volume 10
   20 -> device with volume 20
   10s -> scratch device with volume 10
  */
  @Parameters({
    "aws, m10, m10, c10, c10, true",
    "aws, i10, m10, i10, c10, true", // tserver is ephemeral but not touched.
    "aws, i10, m10, i10, m20, true", // tserver is ephemeral but not touched(2).
    "aws, i10, m10, c10, c10, false", // tserver is ephemeral.
    "aws, i10, m10, i20, c10, false", // tserver is ephemeral(2).
    "aws, m10, i10, c10, i10, true", // master is ephemeral but not touched.
    "aws, m10, i10, m20, i10, true", // master is ephemeral but not touched(2).
    "aws, m10, i10, c10, c10, false", // master is ephemeral.
    "aws, m10, i10, m10, i20, false", // master is ephemeral(2).
    "gcp, m10, c20s, m10, c10s, false", // master has ephemeral storage type.
    "gcp, m10, c20s, m10, m20s, false", // master has ephemeral storage type.
    "aws, m10, c20, m10, c10, false", // decrease volume for master
    "aws, m10, m10, m10, m10, false" // nothing changed
  })
  @Test
  public void testResizeForDedicated(
      String cloudTypeStr,
      String tserverConf,
      String masterConf,
      String targetTserverConf,
      String targetMasterConf,
      boolean expected) {
    modifyToDedicated();
    Common.CloudType cloudType = Common.CloudType.valueOf(cloudTypeStr);
    UniverseDefinitionTaskParams.UserIntent currentIntent = createIntent(cloudType, null, null);
    currentIntent.masterDeviceInfo = currentIntent.deviceInfo.clone();
    currentIntent.dedicatedNodes = true;
    applyConfig(tserverConf, currentIntent, false);
    applyConfig(masterConf, currentIntent, true);
    UniverseDefinitionTaskParams.UserIntent targetIntent = createIntent(cloudType, null, null);
    targetIntent.masterDeviceInfo = targetIntent.deviceInfo.clone();
    targetIntent.dedicatedNodes = true;
    applyConfig(targetTserverConf, targetIntent, false);
    applyConfig(targetMasterConf, targetIntent, true);
    assertEquals(
        expected,
        ResizeNodeParams.checkResizeIsPossible(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid,
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
  }

  private void applyConfig(
      String conf, UniverseDefinitionTaskParams.UserIntent intent, boolean toMaster) {
    char instType = conf.charAt(0);
    String instanceType;
    switch (instType) {
      case 'm':
        instanceType = "m3.medium";
        break;
      case 'c':
        instanceType = "c4.medium";
        break;
      case 'i':
        instanceType = "i3.medium";
        break;
      default:
        throw new IllegalArgumentException("Unknown type " + instType);
    }
    createInstanceType(UUID.fromString(intent.provider), instanceType);
    if (toMaster) {
      intent.masterInstanceType = instanceType;
    } else {
      intent.instanceType = instanceType;
    }
    String diskConf = conf.substring(1);
    boolean useScratch = false;
    if (diskConf.endsWith("s")) {
      useScratch = true;
      diskConf = diskConf.substring(0, diskConf.length() - 2);
    }
    PublicCloudConstants.StorageType storageType =
        chooseStorageType(intent.providerType, useScratch);
    DeviceInfo deviceInfo = toMaster ? intent.masterDeviceInfo : intent.deviceInfo;
    deviceInfo.storageType = storageType;
    deviceInfo.volumeSize = Integer.parseInt(diskConf);
  }

  private PublicCloudConstants.StorageType chooseStorageType(
      Common.CloudType cloudType, boolean useScratch) {
    return Arrays.stream(PublicCloudConstants.StorageType.values())
        .filter(type -> type.getCloudType() == cloudType)
        .filter(type -> useScratch == (type == PublicCloudConstants.StorageType.Scratch))
        .findFirst()
        .get();
  }

  private UniverseDefinitionTaskParams.UserIntent createIntent(
      Common.CloudType cloudType,
      String instanceTypeCode,
      PublicCloudConstants.StorageType storageType) {
    UniverseDefinitionTaskParams.UserIntent currentIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    currentIntent.deviceInfo = new DeviceInfo();
    currentIntent.deviceInfo.volumeSize = 100;
    currentIntent.deviceInfo.numVolumes = 1;
    currentIntent.deviceInfo.storageType = storageType;
    currentIntent.providerType = cloudType;
    switch (cloudType) {
      case aws:
        currentIntent.provider = defaultProvider.getUuid().toString();
        break;
      case gcp:
        currentIntent.provider = gcpProvider.getUuid().toString();
        break;
      case azu:
        currentIntent.provider = azuProvider.getUuid().toString();
        break;
      case kubernetes:
        currentIntent.provider = kubernetesProvider.getUuid().toString();
        break;
    }
    currentIntent.instanceType = instanceTypeCode;
    return currentIntent;
  }

  @Test
  public void testNonRollingUpgradeFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testNonRestartUpgradeFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testNoChangesFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testChangeOnlyGFlagsIsOk() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.clusters.get(0).userIntent.specificGFlags =
        SpecificGFlags.construct(Map.of("master-gflag", "1"), Map.of("tserver-gflag", "2"));
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .withContext(UpgradeTaskBase.RUN_BEFORE_STOPPING)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingNumVolumesFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.clusters.get(0).userIntent.deviceInfo.volumeSize += 10;
    taskParams.clusters.get(0).userIntent.deviceInfo.numVolumes++;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString("Smart resize only supports modifying volumeSize, diskIops, throughput"));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testChangingStorageTypeFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.clusters.get(0).userIntent.deviceInfo.volumeSize += 10;
    taskParams.clusters.get(0).userIntent.deviceInfo.storageType =
        PublicCloudConstants.StorageType.GP2;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString("Smart resize only supports modifying volumeSize, diskIops, throughput"));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testChangingVolume() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, false);

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToTservers()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyCgroup() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.setCgroupSize(NEW_CGROUP_SIZE);
    TaskInfo taskInfo = submitTask(taskParams);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    subTasks.stream()
        .filter(s -> s.getTaskType() == TaskType.ChangeInstanceType)
        .forEach(
            t ->
                assertEquals(
                    String.valueOf(NEW_CGROUP_SIZE), t.getTaskParams().get("cgroupSize").asText()));
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, false, false, false);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertEquals(NEW_CGROUP_SIZE, (int) userIntent.getCgroupSize());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(
            TaskType.ChangeInstanceType,
            Json.newObject().put("cgroupSize", String.valueOf(NEW_CGROUP_SIZE)))
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingBoth() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.diskIops = NEW_DISK_IOPS;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.setCgroupSize(NEW_CGROUP_SIZE);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    subTasks.stream()
        .filter(s -> s.getTaskType() == TaskType.ChangeInstanceType)
        .forEach(
            t ->
                assertEquals(
                    String.valueOf(NEW_CGROUP_SIZE), t.getTaskParams().get("cgroupSize").asText()));
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertEquals(NEW_DISK_IOPS, (int) userIntent.deviceInfo.diskIops);
    assertEquals(NEW_DISK_THROUGHPUT, (int) userIntent.deviceInfo.throughput);
    assertEquals(NEW_CGROUP_SIZE, (int) userIntent.getCgroupSize());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(
            TaskType.ChangeInstanceType,
            Json.newObject().put("cgroupSize", String.valueOf(NEW_CGROUP_SIZE)))
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyThroughput() {
    ResizeNodeParams taskParams = createResizeParams();
    defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.diskIops =
        DEFAULT_DISK_IOPS;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    UniverseDefinitionTaskParams.UserIntent userIntent = taskParams.getPrimaryCluster().userIntent;
    userIntent.providerType = Common.CloudType.aws;
    userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
    userIntent.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, false);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertEquals(DEFAULT_VOLUME_SIZE, (int) userIntent.deviceInfo.volumeSize);
    assertEquals(DEFAULT_DISK_IOPS, (int) userIntent.deviceInfo.diskIops);
    assertEquals(NEW_DISK_THROUGHPUT, (int) userIntent.deviceInfo.throughput);

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToTservers()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstance() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);

    MockUpgrade mockUpgrade = initMockUpgrade();

    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingInstanceWithReadonlyReplica() {
    UniverseDefinitionTaskParams.UserIntent curIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.accessKeyCode = curIntent.accessKeyCode;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe ->
                universe
                    .getNodes()
                    .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE));

    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters =
        Collections.singletonList(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true, true, false);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        // Only primary cluster affected
        .applyToCluster(defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingInstanceWithReadonlyReplicaChanging() {
    UniverseDefinitionTaskParams.UserIntent curIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.accessKeyCode = curIntent.accessKeyCode;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe ->
                universe
                    .getNodes()
                    .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE));

    ResizeNodeParams taskParams = createResizeParams();
    List<UniverseDefinitionTaskParams.Cluster> copyPrimaryCluster =
        Collections.singletonList(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    List<UniverseDefinitionTaskParams.Cluster> copyReadOnlyCluster =
        Collections.singletonList(
            defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0));
    List<UniverseDefinitionTaskParams.Cluster> copyClusterList = new ArrayList<>();
    copyClusterList.addAll(copyPrimaryCluster);
    copyClusterList.addAll(copyReadOnlyCluster);
    taskParams.clusters = copyClusterList;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getReadOnlyClusters().get(0).userIntent.deviceInfo.volumeSize = 250;
    taskParams.getReadOnlyClusters().get(0).userIntent.instanceType = NEW_READ_ONLY_INSTANCE_TYPE;
    taskParams.getReadOnlyClusters().get(0).userIntent.providerType = Common.CloudType.aws;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseDataForReadReplicaClusters(
        true, true, true, true, 250, NEW_READ_ONLY_INSTANCE_TYPE);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        // Primary cluster first
        .applyToCluster(defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        // Now read replica
        .applyToCluster(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingInstanceWithOnlyReadonlyReplicaChanging() {
    UniverseDefinitionTaskParams.UserIntent curIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.accessKeyCode = curIntent.accessKeyCode;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe ->
                universe
                    .getNodes()
                    .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE));

    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getReadOnlyClusters().get(0).userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getReadOnlyClusters().get(0).userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getReadOnlyClusters().get(0).userIntent.providerType = Common.CloudType.aws;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true, false, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        // Only RR cluster
        .applyToCluster(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeOnlyTserver() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToTservers()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeBoth() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.diskIops = NEW_DISK_IOPS;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    taskParams.getPrimaryCluster().userIntent.masterInstanceType = NEW_READ_ONLY_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.volumeSize = NEW_VOLUME_SIZE * 2;
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.diskIops = NEW_DISK_IOPS * 2;
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.throughput = NEW_DISK_THROUGHPUT * 2;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE, NEW_READ_ONLY_INSTANCE_TYPE, NEW_VOLUME_SIZE * 2);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent intent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertNotNull(intent.masterDeviceInfo);
    assertEquals(NEW_DISK_IOPS * 2, (int) (intent.masterDeviceInfo.diskIops));
    assertEquals(NEW_DISK_THROUGHPUT * 2, (int) (intent.masterDeviceInfo.throughput));

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .masterTask(TaskType.ChangeInstanceType)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedResizeMasterDeviceTserverInstance() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.volumeSize = NEW_VOLUME_SIZE * 2;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, NEW_VOLUME_SIZE * 2);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .applyToTservers()
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToMasters()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeOnlyMaster() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.masterInstanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.volumeSize = NEW_VOLUME_SIZE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .masterTask(TaskType.ChangeInstanceType)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToMasters()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeOnlyMasterIops() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    UniverseDefinitionTaskParams.UserIntent userIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    userIntent.providerType = Common.CloudType.aws;
    userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
    userIntent.masterDeviceInfo.diskIops = DEFAULT_DISK_IOPS;
    userIntent.masterDeviceInfo.throughput = DEFAULT_DISK_THROUGHPUT;
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.diskIops = NEW_DISK_IOPS;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent intent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertNotNull(intent.masterDeviceInfo);
    assertEquals(NEW_DISK_IOPS, (int) (intent.masterDeviceInfo.diskIops));
    assertEquals(DEFAULT_DISK_THROUGHPUT, (int) (intent.masterDeviceInfo.throughput));

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToMasters()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeOnlyMasterDisk() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.volumeSize = NEW_VOLUME_SIZE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, NEW_VOLUME_SIZE);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    universe
        .getUniverseDetails()
        .getNodesInCluster(universe.getUniverseDetails().getPrimaryCluster().uuid)
        .forEach(
            node -> {
              if (node.isMaster) {
                assertNotNull(node.lastVolumeUpdateTime);
              } else {
                assertNull(node.lastVolumeUpdateTime);
              }
            });

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToMasters()
        .addTasks(TaskType.PersistResizeNode)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.tserverGFlags = ImmutableMap.of("tserverFlag", "123");
    taskParams.masterGFlags = ImmutableMap.of("masterFlag", "123");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, true, false);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithSpecificGFlags() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams.UserIntent primaryIntent =
              univ.getUniverseDetails().getPrimaryCluster().userIntent;
          primaryIntent.specificGFlags =
              SpecificGFlags.construct(primaryIntent.masterGFlags, primaryIntent.tserverGFlags);
        });
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("masterFlag", "123"), ImmutableMap.of("tserverFlag", "123"));
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, true, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithTserverGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.tserverGFlags = ImmutableMap.of("tserverFlag", "123");
    taskParams.masterGFlags = new HashMap<>();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(false, true, false);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithTserverSpecificGFlags() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams.UserIntent primaryIntent =
              univ.getUniverseDetails().getPrimaryCluster().userIntent;
          primaryIntent.specificGFlags =
              SpecificGFlags.construct(primaryIntent.masterGFlags, primaryIntent.tserverGFlags);
        });
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(
            taskParams.getPrimaryCluster().userIntent.masterGFlags,
            ImmutableMap.of("tserverFlag", "123"));
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(false, true, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithMasterGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.masterGFlags = ImmutableMap.of("masterFlag", "123");
    taskParams.tserverGFlags = new HashMap<>();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, false, false);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithMasterSpecificGFlags() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams.UserIntent primaryIntent =
              univ.getUniverseDetails().getPrimaryCluster().userIntent;
          primaryIntent.specificGFlags =
              SpecificGFlags.construct(primaryIntent.masterGFlags, primaryIntent.tserverGFlags);
        });
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("masterFlag", "123"),
            taskParams.getPrimaryCluster().userIntent.tserverGFlags);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, false, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testRemountDrives() {
    AtomicReference<String> nodeName = new AtomicReference<>();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              NodeDetails node = univ.getUniverseDetails().nodeDetailsSet.iterator().next();
              node.disksAreMountedByUUID = false;
              nodeName.set(node.getNodeName());
            });
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = new ArrayList<>(taskInfo.getSubTasks());
    List<TaskInfo> updateMounts =
        subTasks.stream()
            .filter(t -> t.getTaskType() == TaskType.UpdateMountedDisks)
            .collect(Collectors.toList());

    assertEquals(1, updateMounts.size());
    assertEquals(nodeName.get(), updateMounts.get(0).getTaskParams().get("nodeName").textValue());
    assertEquals(2, updateMounts.get(0).getPosition().intValue());
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTask(TaskType.UpdateMountedDisks, Json.newObject().put("nodeName", nodeName.get()))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangeInstanceForAZ() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UniverseDefinitionTaskParams.AZOverrides azOverrides =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides.setInstanceType(NEW_INSTANCE_TYPE);
    userIntentOverrides.setAzOverrides(Collections.singletonMap(az2.getUuid(), azOverrides));
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(userIntentOverrides);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    String azNodeName =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.getAzUuid().equals(az2.getUuid()))
            .map(n -> n.getNodeName())
            .findFirst()
            .get();

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      if (nodeDetails.getAzUuid().equals(az2.getUuid())) {
        assertEquals(azOverrides.getInstanceType(), nodeDetails.cloudInfo.instance_type);
      } else {
        assertEquals(DEFAULT_INSTANCE_TYPE, nodeDetails.cloudInfo.instance_type);
      }
    }

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        // Only one node affected
        .applyToNodes(Collections.emptySet(), Collections.singleton(azNodeName))
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangeOnlyThroughputForAZ() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UniverseDefinitionTaskParams.AZOverrides azOverrides =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides.setDeviceInfo(new DeviceInfo());
    azOverrides.getDeviceInfo().throughput = NEW_DISK_THROUGHPUT;
    userIntentOverrides.setAzOverrides(Collections.singletonMap(az2.getUuid(), azOverrides));
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(userIntentOverrides);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    String azNodeName =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.getAzUuid().equals(az2.getUuid()))
            .map(n -> n.getNodeName())
            .findFirst()
            .get();

    TaskInfo deviceTask = subTasksByPosition.get(2).get(0);
    JsonNode params = deviceTask.getTaskParams();
    assertEquals(azNodeName, params.get("nodeName").asText());
    JsonNode deviceParams = params.get("deviceInfo");
    DeviceInfo deviceInfo =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo;
    deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    assertEquals(Json.toJson(deviceInfo), deviceParams);
    Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
        .getUniverseDetails()
        .nodeDetailsSet
        .forEach(
            node -> {
              if (node.getAzUuid().equals(az2.getUuid())) {
                assertNotNull(node.lastVolumeUpdateTime);
              } else {
                assertNull(node.lastVolumeUpdateTime);
              }
            });

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToNodes(Collections.emptySet(), Collections.singleton(azNodeName))
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testResetInstanceForAZ() {
    AtomicReference<String> nodeName = new AtomicReference<>();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
                  new UniverseDefinitionTaskParams.UserIntentOverrides();
              UniverseDefinitionTaskParams.AZOverrides azOverrides =
                  new UniverseDefinitionTaskParams.AZOverrides();
              azOverrides.setInstanceType(NEW_INSTANCE_TYPE);
              userIntentOverrides.setAzOverrides(
                  Collections.singletonMap(az2.getUuid(), azOverrides));
              UniverseDefinitionTaskParams.Cluster primaryCluster =
                  univ.getUniverseDetails().getPrimaryCluster();
              primaryCluster.userIntent.setUserIntentOverrides(userIntentOverrides);
              univ.getNodesInCluster(primaryCluster.uuid).stream()
                  .filter(n -> n.getAzUuid().equals(az2.getUuid()))
                  .peek(n -> nodeName.set(n.getNodeName()))
                  .forEach(n -> n.cloudInfo.instance_type = NEW_INSTANCE_TYPE);
            });

    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(null);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        // Only one node affected
        .applyToNodes(Collections.emptySet(), Collections.singleton(nodeName.get()))
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      assertEquals(DEFAULT_INSTANCE_TYPE, nodeDetails.cloudInfo.instance_type);
    }
  }

  private void assertUniverseData(boolean increaseVolume, boolean changeInstance) {
    assertUniverseData(increaseVolume, changeInstance, true, false);
  }

  private void assertUniverseData(
      boolean increaseVolume,
      boolean changeInstance,
      boolean primaryChanged,
      boolean readonlyChanged) {
    // false false means changing throughput or/and iops
    boolean lastVolumeUpdateTimeChanged = increaseVolume || (!increaseVolume && !changeInstance);
    int volumeSize = increaseVolume ? NEW_VOLUME_SIZE : DEFAULT_VOLUME_SIZE;
    String instanceType = changeInstance ? NEW_INSTANCE_TYPE : DEFAULT_INSTANCE_TYPE;
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newIntent = primaryCluster.userIntent;
    if (primaryChanged) {
      assertEquals(volumeSize, newIntent.deviceInfo.volumeSize.intValue());
      assertEquals(instanceType, newIntent.instanceType);
      for (NodeDetails nodeDetails : universe.getNodesInCluster(primaryCluster.uuid)) {
        assertEquals(instanceType, nodeDetails.cloudInfo.instance_type);
        if (lastVolumeUpdateTimeChanged) {
          assertNotNull(nodeDetails.lastVolumeUpdateTime);
        } else {
          assertNull(nodeDetails.lastVolumeUpdateTime);
        }
      }
    }
    if (!universe.getUniverseDetails().getReadOnlyClusters().isEmpty()) {
      UniverseDefinitionTaskParams.Cluster readonlyCluster =
          universe.getUniverseDetails().getReadOnlyClusters().get(0);
      UniverseDefinitionTaskParams.UserIntent readonlyIntent = readonlyCluster.userIntent;
      if (readonlyChanged) {
        assertEquals(volumeSize, readonlyIntent.deviceInfo.volumeSize.intValue());
        assertEquals(instanceType, readonlyIntent.instanceType);
        for (NodeDetails nodeDetails : universe.getNodesInCluster(readonlyCluster.uuid)) {
          assertEquals(instanceType, nodeDetails.cloudInfo.instance_type);
          if (lastVolumeUpdateTimeChanged) {
            assertNotNull(nodeDetails.lastVolumeUpdateTime);
          } else {
            assertNull(nodeDetails.lastVolumeUpdateTime);
          }
        }
      } else {
        assertEquals(DEFAULT_VOLUME_SIZE, readonlyIntent.deviceInfo.volumeSize.intValue());
        assertEquals(DEFAULT_INSTANCE_TYPE, readonlyIntent.instanceType);
        for (NodeDetails nodeDetails : universe.getNodesInCluster(readonlyCluster.uuid)) {
          assertEquals(DEFAULT_INSTANCE_TYPE, nodeDetails.cloudInfo.instance_type);
          assertNull(nodeDetails.lastVolumeUpdateTime);
        }
      }
    }
  }

  private void assertUniverseDataForReadReplicaClusters(
      boolean increaseVolume,
      boolean changeInstance,
      boolean primaryChanged,
      boolean readonlyChanged,
      Integer readReplicaVolumeSize,
      String readReplicaInstanceType) {
    int volumeSize = increaseVolume ? NEW_VOLUME_SIZE : DEFAULT_VOLUME_SIZE;
    String instanceType = changeInstance ? NEW_INSTANCE_TYPE : DEFAULT_INSTANCE_TYPE;
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent newIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (primaryChanged) {
      assertEquals(volumeSize, newIntent.deviceInfo.volumeSize.intValue());
      assertEquals(instanceType, newIntent.instanceType);
    }
    if (!universe.getUniverseDetails().getReadOnlyClusters().isEmpty()) {
      UniverseDefinitionTaskParams.UserIntent readonlyIntent =
          universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent;
      if (readonlyChanged) {
        assertEquals(readReplicaVolumeSize, readonlyIntent.deviceInfo.volumeSize);
        assertEquals(readReplicaInstanceType, readonlyIntent.instanceType);
      } else {
        assertEquals(DEFAULT_VOLUME_SIZE, readonlyIntent.deviceInfo.volumeSize.intValue());
        assertEquals(DEFAULT_INSTANCE_TYPE, readonlyIntent.instanceType);
      }
    }
  }

  private UpgradeTaskBase.UpgradeContext instanceChangeContext(MockUpgrade mockUpgrade) {
    return UpgradeTaskBase.UpgradeContext.builder()
        .reconfigureMaster(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor
                > 1)
        .postAction(
            node -> {
              mockUpgrade.addTask(TaskType.UpdateNodeDetails, null);
            })
        .build();
  }

  private TaskInfo submitTask(ResizeNodeParams requestParams) {
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    return submitTask(requestParams, TaskType.ResizeNode, commissioner, -1);
  }

  private ResizeNodeParams createResizeParams() {
    ResizeNodeParams taskParams = new ResizeNodeParams();
    RuntimeConfigEntry.upsertGlobal("yb.internal.allow_unsupported_instances", "true");
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    return taskParams;
  }

  private ResizeNodeParams createResizeParamsForCloud() {
    ResizeNodeParams taskParams = new ResizeNodeParams();
    RuntimeConfigEntry.upsertGlobal("yb.cloud.enabled", "true");
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    return taskParams;
  }

  private void createInstanceType(UUID providerId, String type) {
    InstanceType.InstanceTypeDetails instanceTypeDetails = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    volumeDetails.volumeSizeGB = 100;
    volumeDetails.mountPath = "/";
    instanceTypeDetails.volumeDetailsList = Collections.singletonList(volumeDetails);
    InstanceType.upsert(providerId, type, 1, 100d, instanceTypeDetails);
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(ResizeNode.class);
  }

  private void assertDedicatedIntent(
      String newInstanceType,
      int newVolumeSize,
      String newMasterInstanceType,
      int newMasterVolumeSize) {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertEquals(newInstanceType, userIntent.instanceType);
    assertEquals(newVolumeSize, (int) userIntent.deviceInfo.volumeSize);
    assertEquals(newMasterInstanceType, userIntent.masterInstanceType);
    assertEquals(newMasterVolumeSize, (int) userIntent.masterDeviceInfo.volumeSize);
    universe
        .getUniverseDetails()
        .nodeDetailsSet
        .forEach(
            node -> {
              if (node.dedicatedTo == MASTER) {
                assertEquals(newMasterInstanceType, node.cloudInfo.instance_type);
              } else {
                assertEquals(newInstanceType, node.cloudInfo.instance_type);
              }
            });
  }

  private Pair<Integer, Integer> modifyToDedicated() {
    int currentNodeCount = defaultUniverse.getUniverseDetails().nodeDetailsSet.size();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.dedicatedNodes = true;
              userIntent.masterInstanceType = userIntent.instanceType;
              userIntent.masterDeviceInfo = userIntent.deviceInfo.clone();
              String masterLeader = universe.getMasterLeaderHostText();
              universe
                  .getUniverseDetails()
                  .nodeDetailsSet
                  .forEach(
                      node -> {
                        node.isMaster = false;
                      });
              PlacementInfoUtil.SelectMastersResult selectMastersResult =
                  PlacementInfoUtil.selectMasters(
                      masterLeader,
                      universe.getNodes(),
                      null,
                      true,
                      universe.getUniverseDetails().clusters);
              AtomicInteger nodeIdx = new AtomicInteger(universe.getNodes().size());
              AtomicInteger cnt = new AtomicInteger();
              selectMastersResult.addedMasters.forEach(
                  newMaster -> {
                    newMaster.cloudInfo.private_ip = "1.1.1." + cnt.incrementAndGet();
                    universe.getUniverseDetails().nodeDetailsSet.add(newMaster);
                    newMaster.state = NodeDetails.NodeState.Live;
                    newMaster.nodeName = "host-n" + nodeIdx.incrementAndGet();
                  });
              PlacementInfoUtil.dedicateNodes(universe.getUniverseDetails().nodeDetailsSet);
            });
    int tserverNodes = 0;
    int masterNodes = 0;
    for (NodeDetails node : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      if (node.isMaster) {
        assertEquals(MASTER, node.dedicatedTo);
        assertFalse(node.isTserver);
        masterNodes++;
      } else {
        assertEquals(TSERVER, node.dedicatedTo);
        assertTrue(node.isTserver);
        tserverNodes++;
      }
    }
    assertTrue(defaultUniverse.getUniverseDetails().nodeDetailsSet.size() > currentNodeCount);
    assertEquals(
        tserverNodes + masterNodes, defaultUniverse.getUniverseDetails().nodeDetailsSet.size());
    return new Pair<>(tserverNodes, masterNodes);
  }

  private void assertGflags(
      boolean updateMasterGflags, boolean updateTserverGflags, boolean useSpecificGFlags) {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newIntent = primaryCluster.userIntent;
    if (updateMasterGflags) {
      if (useSpecificGFlags) {
        assertEquals(
            newIntent.specificGFlags.getGFlags(null, MASTER),
            new HashMap<>(ImmutableMap.of("masterFlag", "123")));
      }
      assertEquals(newIntent.masterGFlags, ImmutableMap.of("masterFlag", "123"));
    }
    if (updateTserverGflags) {
      if (useSpecificGFlags) {
        assertEquals(
            newIntent.specificGFlags.getGFlags(null, TSERVER),
            new HashMap<>(ImmutableMap.of("tserverFlag", "123")));
      }
      assertEquals(newIntent.tserverGFlags, ImmutableMap.of("tserverFlag", "123"));
    }
  }
}
