// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.*;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static com.yugabyte.yw.models.helpers.TaskType.AnsibleClusterServerCtl;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseModifyBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
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
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BiConsumer;
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

  // Tasks for RF1 configuration do not create sub-tasks for
  // leader blacklisting. So create two PLACEHOLDER indexes
  // as well as two separate base task sequences
  private static final int PLACEHOLDER_INDEX = 4;
  private static final int PLACEHOLDER_INDEX_RF1 = 2;

  private static final List<TaskType> TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.SetNodeState);

  private static final List<TaskType> PROCESS_START_SEQ =
      ImmutableList.of(
          AnsibleClusterServerCtl, TaskType.WaitForServer, TaskType.WaitForServerReady);

  private static final List<TaskType> RESIZE_VOLUME_SEQ =
      ImmutableList.of(TaskType.InstanceActions);

  @InjectMocks private ResizeNode resizeNode;

  @Override
  @Before
  public void setUp() {
    super.setUp();
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
    } catch (Exception ignored) {
    }

    createInstanceType(defaultProvider.getUuid(), DEFAULT_INSTANCE_TYPE);
    createInstanceType(defaultProvider.getUuid(), NEW_INSTANCE_TYPE);
    createInstanceType(defaultProvider.getUuid(), NEW_READ_ONLY_INSTANCE_TYPE);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("underreplicated_tablets", Json.newArray());
    when(mockNodeUIApiHelper.getRequest(anyString())).thenReturn(bodyJson);
    UniverseModifyBaseTest.mockGetMasterRegistrationResponses(
        mockClient,
        ImmutableList.of("10.0.0.1", "10.0.0.2", "10.0.0.3", "1.1.1.1", "1.1.1.2", "1.1.1.3"));
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

    createInstanceType(UUID.fromString(currentIntent.provider), targetInstanceTypeCode);
    assertEquals(
        expected,
        ResizeNodeParams.checkResizeIsPossible(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid,
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter(),
            true));
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
            mockBaseTaskDependencies.getConfGetter(),
            true));
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
            mockBaseTaskDependencies.getConfGetter(),
            true));
    // Just changing instance type is available, even within cooldown window.
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntentJustType,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter(),
            true));
    // Changing window size.
    RuntimeConfigEntry.upsertGlobal("yb.aws.disk_resize_cooldown_hours", "1");
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter(),
            true));
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
    Pair<Integer, Integer> counts = modifyToDedicated();
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
            mockBaseTaskDependencies.getConfGetter(),
            true));
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
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testNonRestartUpgradeFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testNoChangesFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testChangingNumVolumesFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.clusters.get(0).userIntent.deviceInfo.volumeSize += 10;
    taskParams.clusters.get(0).userIntent.deviceInfo.numVolumes++;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
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
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
        containsString("Smart resize only supports modifying volumeSize, diskIops, throughput"));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testChangingVolume() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position =
        assertAllNodesActions(
            0,
            subTasksByPosition,
            TaskType.SetNodeState,
            TaskType.InstanceActions,
            TaskType.SetNodeState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.PersistResizeNode);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateSucceeded);

    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, false);
  }

  private int assertAllNodesActions(
      int position, Map<Integer, List<TaskInfo>> subTasksByPosition, TaskType... types) {
    for (TaskType type : types) {
      List<TaskInfo> tasks = subTasksByPosition.get(position++);
      assertEquals(defaultUniverse.getNodes().size(), tasks.size());
      assertEquals(type, tasks.get(0).getTaskType());
    }
    return position;
  }

  @Test
  public void testChangingBoth() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.diskIops = NEW_DISK_IOPS;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(subTasks, true, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertEquals(NEW_DISK_IOPS, (int) userIntent.deviceInfo.diskIops);
    assertEquals(NEW_DISK_THROUGHPUT, (int) userIntent.deviceInfo.throughput);
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
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, false);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertEquals(DEFAULT_VOLUME_SIZE, (int) userIntent.deviceInfo.volumeSize);
    assertEquals(DEFAULT_DISK_IOPS, (int) userIntent.deviceInfo.diskIops);
    assertEquals(NEW_DISK_THROUGHPUT, (int) userIntent.deviceInfo.throughput);
  }

  @Test
  public void testChangingOnlyInstance() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(subTasks, false, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
  }

  @Test
  public void testNoWaitForMasterLeaderForRF1() {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              univ.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor = 1;
            });
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(0, subTasks, true, true, false, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);
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
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    // it checks only primary nodes are changed
    assertTasksSequence(subTasks, true, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true, true, false);
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
    assertSubtasks(subTasks, counts.getFirst(), counts.getFirst(), counts.getFirst());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE);
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
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertSubtasks(
        subTasks,
        counts.getFirst() + counts.getSecond(),
        counts.getFirst() + counts.getSecond(),
        counts.getFirst() + counts.getSecond());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE, NEW_READ_ONLY_INSTANCE_TYPE, NEW_VOLUME_SIZE * 2);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent intent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertNotNull(intent.masterDeviceInfo);
    assertEquals(NEW_DISK_IOPS * 2, (int) (intent.masterDeviceInfo.diskIops));
    assertEquals(NEW_DISK_THROUGHPUT * 2, (int) (intent.masterDeviceInfo.throughput));
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
    assertSubtasks(subTasks, counts.getFirst(), counts.getFirst(), counts.getSecond());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, NEW_VOLUME_SIZE * 2);
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
    assertSubtasks(subTasks, counts.getSecond(), counts.getSecond(), counts.getSecond());
    assertDedicatedIntent(
        DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE);
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
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertSubtasks(subTasks, 0, 0, counts.getSecond());
    assertDedicatedIntent(
        DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent intent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertNotNull(intent.masterDeviceInfo);
    assertEquals(NEW_DISK_IOPS, (int) (intent.masterDeviceInfo.diskIops));
    assertEquals(DEFAULT_DISK_THROUGHPUT, (int) (intent.masterDeviceInfo.throughput));
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
    assertSubtasks(subTasks, 0, 0, counts.getSecond());
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
  }

  @Test
  public void testChangingOnlyInstanceWithGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.tserverGFlags = ImmutableMap.of("tserverFlag", "123");
    taskParams.masterGFlags = ImmutableMap.of("masterFlag", "123");
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(0, subTasks, false, true, true, false, true, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, true, false);
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
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(0, subTasks, false, true, true, false, true, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, true, true);
  }

  @Test
  public void testChangingOnlyInstanceWithTserverGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.tserverGFlags = ImmutableMap.of("tserverFlag", "123");
    taskParams.masterGFlags = new HashMap<>();
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(0, subTasks, false, true, true, false, false, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(false, true, false);
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
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(0, subTasks, false, true, true, false, false, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(false, true, true);
  }

  @Test
  public void testChangingOnlyInstanceWithMasterGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.masterGFlags = ImmutableMap.of("masterFlag", "123");
    taskParams.tserverGFlags = new HashMap<>();
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(0, subTasks, false, true, true, false, true, false);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, false, false);
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
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(0, subTasks, false, true, true, false, true, false);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, false, true);
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

  private void assertSubtasks(
      List<TaskInfo> subTasks, int restartsCount, int changeInstanceCounts, int resizeCounts) {
    int actualRestarts = 0;
    int actualChangeInstance = 0;
    int actualResizes = 0;
    for (TaskInfo subTask : subTasks) {
      if (subTask.getTaskType() == AnsibleClusterServerCtl) {
        actualRestarts++;
      } else if (subTask.getTaskType() == TaskType.ChangeInstanceType) {
        actualChangeInstance++;
      } else if (subTask.getTaskType() == TaskType.InstanceActions) {
        actualResizes++;
      }
    }
    assertEquals(restartsCount, actualRestarts / 2); // Counting stop and start separately.
    assertEquals(changeInstanceCounts, actualChangeInstance);
    assertEquals(resizeCounts, actualResizes);
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
    assertEquals(nodeName.get(), updateMounts.get(0).getDetails().get("nodeName").textValue());
    assertEquals(0L, (long) updateMounts.get(0).getPosition());
    assertTasksSequence(1, subTasks, true, true, true, false);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);
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

    List<TaskType> taskTypes = new ArrayList<>(TASK_SEQUENCE);
    Map<Integer, Map<String, Object>> paramsForTask = new HashMap<>();
    createTasksTypesForNode(
        false, false, false, true, taskTypes, paramsForTask, true, false, false);

    for (int i = 0; i < taskTypes.size(); i++) {
      TaskType taskType = taskTypes.get(i);
      Map<String, Object> params = new HashMap<>(paramsForTask.getOrDefault(i, new HashMap<>()));
      if (!NON_NODE_TASKS.contains(taskType)) {
        params.put("nodeName", azNodeName);
      }
      paramsForTask.put(i, params);
    }
    taskTypes.add(TaskType.PersistResizeNode);
    taskTypes.add(TaskType.UniverseUpdateSucceeded);
    assertTasksSequence(1, subTasksByPosition, taskTypes, paramsForTask, false);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      if (nodeDetails.getAzUuid().equals(az2.getUuid())) {
        assertEquals(azOverrides.getInstanceType(), nodeDetails.cloudInfo.instance_type);
      } else {
        assertEquals(DEFAULT_INSTANCE_TYPE, nodeDetails.cloudInfo.instance_type);
      }
    }
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
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.SetNodeState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.InstanceActions);
    assertTaskType(subTasksByPosition.get(position++), TaskType.SetNodeState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.PersistResizeNode);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateSucceeded);
    TaskInfo deviceTask = subTasksByPosition.get(1).get(0);
    JsonNode params = deviceTask.getDetails();
    assertEquals(azNodeName, params.get("nodeName").asText());
    JsonNode deviceParams = params.get("deviceInfo");
    DeviceInfo deviceInfo =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo;
    deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    assertEquals(Json.toJson(deviceInfo), deviceParams);
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
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    List<TaskType> taskTypes = new ArrayList<>(TASK_SEQUENCE);
    Map<Integer, Map<String, Object>> paramsForTask = new HashMap<>();
    createTasksTypesForNode(
        false, false, false, true, taskTypes, paramsForTask, true, false, false);

    for (int i = 0; i < taskTypes.size(); i++) {
      TaskType taskType = taskTypes.get(i);
      Map<String, Object> params = new HashMap<>(paramsForTask.getOrDefault(i, new HashMap<>()));
      if (!NON_NODE_TASKS.contains(taskType)) {
        params.put("nodeName", nodeName.get());
      }
      paramsForTask.put(i, params);
    }
    taskTypes.add(TaskType.PersistResizeNode);
    taskTypes.add(TaskType.UniverseUpdateSucceeded);
    assertTasksSequence(1, subTasksByPosition, taskTypes, paramsForTask, false);
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

  private void assertTasksSequence(
      List<TaskInfo> subTasks, boolean increaseVolume, boolean changeInstance) {
    assertTasksSequence(0, subTasks, increaseVolume, changeInstance, true, false);
  }

  private void assertTasksSequence(
      int startPosition,
      List<TaskInfo> subTasks,
      boolean increaseVolume,
      boolean changeInstance,
      boolean waitForMasterLeader,
      boolean isRf1) {
    assertTasksSequence(
        startPosition,
        subTasks,
        increaseVolume,
        changeInstance,
        waitForMasterLeader,
        isRf1,
        false,
        false);
  }

  protected void assertTasksSequence(
      int startPosition,
      List<TaskInfo> subTasks,
      boolean increaseVolume,
      boolean changeInstance,
      boolean waitForMasterLeader,
      boolean isRf1,
      boolean updateMasterGflags,
      boolean updateTserverGflags) {
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    assertEquals(subTasks.size(), subTasksByPosition.size());
    int position = startPosition;
    List<TaskType> expectedTaskTypes = new ArrayList<>();
    Map<Integer, Map<String, Object>> expectedParams = new HashMap<>();
    expectedTaskTypes.add(TaskType.ModifyBlackList); // removing all tservers from BL

    createTasksSequence(
        EITHER,
        increaseVolume,
        changeInstance,
        waitForMasterLeader,
        isRf1,
        updateMasterGflags,
        updateTserverGflags,
        (taskType, params) -> {
          expectedTaskTypes.add(taskType);
          expectedParams.put(expectedTaskTypes.size() - 1, params);
        });
    createTasksSequence(
        TSERVER,
        increaseVolume,
        changeInstance,
        false,
        isRf1,
        updateMasterGflags,
        updateTserverGflags,
        (taskType, params) -> {
          expectedTaskTypes.add(taskType);
          expectedParams.put(expectedTaskTypes.size() - 1, params);
        });
    expectedTaskTypes.add(TaskType.PersistResizeNode);
    if (updateMasterGflags || updateTserverGflags) {
      expectedTaskTypes.add(TaskType.UpdateAndPersistGFlags);
    }
    expectedTaskTypes.add(TaskType.UniverseUpdateSucceeded);
    if (!isRf1) {
      expectedTaskTypes.add(TaskType.ModifyBlackList);
    }
    assertTasksSequence(position, subTasksByPosition, expectedTaskTypes, expectedParams, true);
  }

  private void createTasksSequence(
      ServerType serverType,
      boolean increaseVolume,
      boolean changeInstance,
      boolean waitForMasterLeader,
      boolean isRf1,
      boolean updateMasterGflags,
      boolean updateTserverGflags,
      BiConsumer<TaskType, Map<String, Object>> taskCallback) {
    List<Integer> nodeIndexes =
        serverType == EITHER ? Arrays.asList(1, 3, 2) : Collections.singletonList(4);

    for (Integer nodeIndex : nodeIndexes) {
      String nodeName = String.format("host-n%d", nodeIndex);
      Map<Integer, Map<String, Object>> paramsForTask = new HashMap<>();
      List<TaskType> taskTypesSequence = new ArrayList<>(TASK_SEQUENCE);
      if (isRf1) {
        taskTypesSequence.removeIf(
            type ->
                type == TaskType.ModifyBlackList
                    || type == TaskType.WaitForLeaderBlacklistCompletion);
      }
      createTasksTypesForNode(
          isRf1,
          serverType != EITHER,
          increaseVolume,
          changeInstance,
          taskTypesSequence,
          paramsForTask,
          waitForMasterLeader,
          updateMasterGflags,
          updateTserverGflags);
      int idx = 0;
      boolean stopped = false;
      for (TaskType taskType : taskTypesSequence) {
        Map<String, Object> expectedParams = new HashMap<>();
        expectedParams.putAll(paramsForTask.getOrDefault(idx++, Collections.emptyMap()));
        if (!NON_NODE_TASKS.contains(taskType)) {
          expectedParams.put("nodeName", nodeName);
        }
        if (taskType == TaskType.SetNodeState) {
          expectedParams.put("state", !stopped ? "Resizing" : "Live");
          stopped = !stopped;
        }
        taskCallback.accept(taskType, expectedParams);
      }
    }
  }

  private List<TaskType> getTasksForInstanceResize(
      boolean updateMasterGflags, boolean updateTserverGflags, boolean onlyTserver) {
    List<TaskType> tasks = new ArrayList<>();
    tasks.add(TaskType.ChangeInstanceType);
    if (updateMasterGflags && !onlyTserver) {
      tasks.add(TaskType.AnsibleConfigureServers);
    }
    if (updateTserverGflags) {
      tasks.add(TaskType.AnsibleConfigureServers);
    }
    return tasks;
  }

  private void createTasksTypesForNode(
      boolean isRf1,
      boolean onlyTserver,
      boolean increaseVolume,
      boolean changeInstance,
      List<TaskType> taskTypesSequence,
      Map<Integer, Map<String, Object>> paramsForTask,
      boolean waitForMasterLeader,
      boolean updateMasterGflags,
      boolean updateTserverGflags) {
    List<TaskType> nodeUpgradeTasks = new ArrayList<>();
    if (changeInstance) {
      nodeUpgradeTasks.addAll(
          getTasksForInstanceResize(updateMasterGflags, updateTserverGflags, onlyTserver));
    }
    if (increaseVolume) {
      nodeUpgradeTasks.addAll(RESIZE_VOLUME_SEQ);
    }
    List<UniverseTaskBase.ServerType> processTypes =
        onlyTserver ? ImmutableList.of(TSERVER) : ImmutableList.of(MASTER, TSERVER);

    int index = isRf1 ? PLACEHOLDER_INDEX_RF1 : PLACEHOLDER_INDEX;
    for (ServerType processType : processTypes) {
      paramsForTask.put(
          index, ImmutableMap.of("process", processType.name().toLowerCase(), "command", "stop"));
      taskTypesSequence.add(index++, AnsibleClusterServerCtl);
      if (processType == MASTER && waitForMasterLeader) {
        taskTypesSequence.add(index++, TaskType.WaitForMasterLeader);
        taskTypesSequence.add(index++, TaskType.ChangeMasterConfig);
        paramsForTask.put(index, ImmutableMap.of("opType", "RemoveMaster"));
      }
    }
    // node upgrade tasks
    taskTypesSequence.addAll(index, nodeUpgradeTasks);
    index += nodeUpgradeTasks.size();

    for (ServerType processType : processTypes) {
      List<TaskType> startSequence = new ArrayList<>(PROCESS_START_SEQ);
      if (processType == MASTER && waitForMasterLeader) {
        startSequence.add(2, TaskType.ChangeMasterConfig);
        startSequence.add(3, TaskType.WaitForFollowerLag);
      }
      for (TaskType taskType : startSequence) {
        if (taskType == AnsibleClusterServerCtl) {
          paramsForTask.put(
              index,
              ImmutableMap.of(
                  "process",
                  processType.name().toLowerCase(),
                  "command",
                  "start",
                  "checkVolumesAttached",
                  processType == TSERVER));
        } else if (taskType == TaskType.ChangeMasterConfig) {
          paramsForTask.put(index, ImmutableMap.of("opType", "AddMaster"));
        } else if (taskType == TaskType.WaitForFollowerLag) {
          paramsForTask.put(index, ImmutableMap.of());
        } else {
          paramsForTask.put(index, ImmutableMap.of("serverType", processType.name()));
        }
        taskTypesSequence.add(index++, taskType);
      }
    }
    // Skipping WaitForEncryptionKeyInMemory and ModifyBlacklist (if not RF1).
    index += isRf1 ? 1 : 2;
    for (ServerType processType : processTypes) {
      taskTypesSequence.add(index++, TaskType.WaitForFollowerLag);
    }
    if (changeInstance) {
      taskTypesSequence.add(index++, TaskType.UpdateNodeDetails);
    }
  }

  private TaskInfo submitTask(ResizeNodeParams requestParams) {
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
}
