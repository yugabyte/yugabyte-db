// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.EITHER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
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
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.ListMastersResponse;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class ResizeNodeTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static final String DEFAULT_INSTANCE_TYPE = "c3.medium";
  private static final String NEW_INSTANCE_TYPE = "c4.medium";
  private static final String NEW_READ_ONLY_INSTANCE_TYPE = "c3.small";

  private static final int DEFAULT_VOLUME_SIZE = 100;
  private static final int NEW_VOLUME_SIZE = 200;

  // Tasks for RF1 configuration do not create sub-tasks for
  // leader blacklisting. So create two PLACEHOLDER indexes
  // as well as two separate base task sequences
  private static final int PLACEHOLDER_INDEX = 3;

  private static final int PLACEHOLDER_INDEX_RF1 = 1;

  private static final List<TaskType> TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.SetNodeState);

  private static final List<TaskType> TASK_SEQUENCE_RF1 =
      ImmutableList.of(
          TaskType.SetNodeState, TaskType.WaitForEncryptionKeyInMemory, TaskType.SetNodeState);

  private static final List<TaskType> PROCESS_START_SEQ =
      ImmutableList.of(
          TaskType.AnsibleClusterServerCtl, TaskType.WaitForServer, TaskType.WaitForServerReady);

  private static final List<TaskType> RESIZE_VOLUME_SEQ =
      ImmutableList.of(TaskType.InstanceActions);

  private static final List<TaskType> UPDATE_INSTANCE_TYPE_SEQ =
      ImmutableList.of(TaskType.ChangeInstanceType, TaskType.UpdateNodeDetails);

  @InjectMocks private ResizeNode resizeNode;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    resizeNode.setUserTaskUUID(UUID.randomUUID());
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            universe -> {
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.deviceInfo = new DeviceInfo();
              userIntent.deviceInfo.numVolumes = 1;
              userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
              userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
              userIntent.provider = defaultProvider.uuid.toString();
              universe
                  .getNodes()
                  .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE);
            });
    try {
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
      when(mockConfigHelper.getAWSInstancePrefixesSupported())
          .thenReturn(Arrays.asList("c5.", "c5d.", "c4.", "c3."));
    } catch (Exception ignored) {
    }

    createInstanceType(defaultProvider.uuid, DEFAULT_INSTANCE_TYPE);
    createInstanceType(defaultProvider.uuid, NEW_INSTANCE_TYPE);
    createInstanceType(defaultProvider.uuid, NEW_READ_ONLY_INSTANCE_TYPE);
  }

  @Override
  protected PlacementInfo createPlacementInfo() {
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, placementInfo, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.uuid, placementInfo, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.uuid, placementInfo, 1, 2, false);
    return placementInfo;
  }

  @Parameters({
    "aws, 0, 10, m3.medium, m3.medium, true",
    "gcp, 0, 10, m3.medium, m3.medium, true",
    "aws, 0, 10, m3.medium, c4.medium, true",
    "aws, 0, -10, m3.medium, m3.medium, false", // decrease volume
    "aws, 1, 10, m3.medium, m3.medium, false", // change num of volumes
    "azu, 0, 10, m3.medium, m3.medium, false", // wrong provider
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
    Common.CloudType cloudType = Common.CloudType.valueOf(cloudTypeStr);
    PublicCloudConstants.StorageType storageType =
        chooseStorageType(cloudType, curInstanceTypeCode.equals("scratch"));
    UniverseDefinitionTaskParams.UserIntent currentIntent =
        createIntent(cloudType, curInstanceTypeCode, storageType);

    UniverseDefinitionTaskParams.UserIntent targetIntent =
        createIntent(cloudType, targetInstanceTypeCode, storageType);
    targetIntent.deviceInfo.volumeSize += volumeSizeDiff;
    targetIntent.deviceInfo.numVolumes += numOfVolumesDiff;

    createInstanceType(UUID.fromString(currentIntent.provider), targetInstanceTypeCode);
    assertEquals(
        expected,
        ResizeNodeParams.checkResizeIsPossible(
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getRuntimeConfigFactory(),
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
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getRuntimeConfigFactory(),
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
    currentIntent.provider = defaultProvider.uuid.toString();
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
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertTasksSequence(subTasks, true, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);
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
            defaultUniverse.universeUUID,
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
    userIntent.regionList = ImmutableList.of(region.uuid);
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
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
    userIntent.regionList = ImmutableList.of(region.uuid);
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
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
    userIntent.regionList = ImmutableList.of(region.uuid);
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            universe ->
                universe
                    .getNodes()
                    .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE));

    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters =
        Collections.singletonList(
            defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0));
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
    taskParams.getPrimaryCluster().userIntent.masterInstanceType = NEW_READ_ONLY_INSTANCE_TYPE;
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.volumeSize = NEW_VOLUME_SIZE * 2;
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
  }

  private void assertDedicatedIntent(
      String newInstanceType,
      int newVolumeSize,
      String newMasterInstanceType,
      int newMasterVolumeSize) {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.universeUUID);
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
      if (subTask.getTaskType() == TaskType.AnsibleClusterServerCtl) {
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
            defaultUniverse.universeUUID,
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
                      masterLeader, universe.getNodes(), null, true, userIntent);
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
            defaultUniverse.universeUUID,
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
        subTasks
            .stream()
            .filter(t -> t.getTaskType() == TaskType.UpdateMountedDisks)
            .collect(Collectors.toList());

    assertEquals(1, updateMounts.size());
    assertEquals(nodeName.get(), updateMounts.get(0).getTaskDetails().get("nodeName").textValue());
    assertEquals(0, updateMounts.get(0).getPosition());
    assertTasksSequence(1, subTasks, true, true, true, false);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);
  }

  private void assertUniverseData(boolean increaseVolume, boolean changeInstance) {
    assertUniverseData(increaseVolume, changeInstance, true, false);
  }

  private void assertUniverseData(
      boolean increaseVolume,
      boolean changeInstance,
      boolean primaryChanged,
      boolean readonlyChanged) {
    int volumeSize = increaseVolume ? NEW_VOLUME_SIZE : DEFAULT_VOLUME_SIZE;
    String instanceType = changeInstance ? NEW_INSTANCE_TYPE : DEFAULT_INSTANCE_TYPE;
    Universe universe = Universe.getOrBadRequest(defaultUniverse.universeUUID);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newIntent = primaryCluster.userIntent;
    if (primaryChanged) {
      assertEquals(volumeSize, newIntent.deviceInfo.volumeSize.intValue());
      assertEquals(instanceType, newIntent.instanceType);
      for (NodeDetails nodeDetails : universe.getNodesInCluster(primaryCluster.uuid)) {
        assertEquals(instanceType, nodeDetails.cloudInfo.instance_type);
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
        }
      } else {
        assertEquals(DEFAULT_VOLUME_SIZE, readonlyIntent.deviceInfo.volumeSize.intValue());
        assertEquals(DEFAULT_INSTANCE_TYPE, readonlyIntent.instanceType);
        for (NodeDetails nodeDetails : universe.getNodesInCluster(readonlyCluster.uuid)) {
          assertEquals(DEFAULT_INSTANCE_TYPE, nodeDetails.cloudInfo.instance_type);
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
    Universe universe = Universe.getOrBadRequest(defaultUniverse.universeUUID);
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
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    assertEquals(subTasks.size(), subTasksByPosition.size());
    int position = startPosition;
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);

    position =
        assertTasksSequence(
            subTasksByPosition,
            EITHER,
            position,
            increaseVolume,
            changeInstance,
            waitForMasterLeader,
            isRf1);
    position =
        assertTasksSequence(
            subTasksByPosition, TSERVER, position, increaseVolume, changeInstance, false, isRf1);
    assertTaskType(subTasksByPosition.get(position++), TaskType.PersistResizeNode);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateSucceeded);
    assertEquals(position, subTasks.size() - 1);
  }

  private int assertTasksSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int position,
      boolean increaseVolume,
      boolean changeInstance,
      boolean waitForMasterLeader,
      boolean isRf1) {
    List<Integer> nodeIndexes =
        serverType == EITHER ? Arrays.asList(1, 3, 2) : Collections.singletonList(4);

    for (Integer nodeIndex : nodeIndexes) {
      String nodeName = String.format("host-n%d", nodeIndex);
      Map<Integer, Map<String, Object>> paramsForTask = new HashMap<>();
      List<TaskType> taskTypesSequence =
          isRf1 ? new ArrayList<>(TASK_SEQUENCE_RF1) : new ArrayList<>(TASK_SEQUENCE);
      createTasksTypesForNode(
          serverType != EITHER,
          increaseVolume,
          changeInstance,
          taskTypesSequence,
          paramsForTask,
          waitForMasterLeader,
          isRf1);

      int idx = 0;
      log.debug(nodeName + " :" + taskTypesSequence);
      log.debug(
          "current:"
              + IntStream.range(position, position + taskTypesSequence.size())
                  .mapToObj(p -> subTasksByPosition.get(p).get(0).getTaskType())
                  .collect(Collectors.toList()));

      for (TaskType expectedTaskType : taskTypesSequence) {
        List<TaskInfo> tasks = subTasksByPosition.get(position++);
        TaskType taskType = tasks.get(0).getTaskType();
        assertEquals(1, tasks.size());
        assertEquals(
            String.format("Host %s at %d positon", nodeName, idx), expectedTaskType, taskType);
        if (!NON_NODE_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));
          assertValues.putAll(paramsForTask.getOrDefault(idx, Collections.emptyMap()));
          log.debug("checking " + tasks.get(0).getTaskType());
          assertNodeSubTask(tasks, assertValues);
        }
        idx++;
      }
    }
    return position;
  }

  private void createTasksTypesForNode(
      boolean onlyTserver,
      boolean increaseVolume,
      boolean changeInstance,
      List<TaskType> taskTypesSequence,
      Map<Integer, Map<String, Object>> paramsForTask,
      boolean waitForMasterLeader,
      boolean isRf1) {
    List<TaskType> nodeUpgradeTasks = new ArrayList<>();
    if (increaseVolume) {
      nodeUpgradeTasks.addAll(RESIZE_VOLUME_SEQ);
    }
    if (changeInstance) {
      nodeUpgradeTasks.add(TaskType.ChangeInstanceType);
    }
    List<UniverseDefinitionTaskBase.ServerType> processTypes =
        onlyTserver ? ImmutableList.of(TSERVER) : ImmutableList.of(MASTER, TSERVER);

    int index = isRf1 ? PLACEHOLDER_INDEX_RF1 : PLACEHOLDER_INDEX;
    for (ServerType processType : processTypes) {
      paramsForTask.put(
          index, ImmutableMap.of("process", processType.name().toLowerCase(), "command", "stop"));
      taskTypesSequence.add(index++, TaskType.AnsibleClusterServerCtl);
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
      }
      for (TaskType taskType : startSequence) {
        if (taskType == TaskType.AnsibleClusterServerCtl) {
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
        } else {
          paramsForTask.put(index, ImmutableMap.of("serverType", processType.name()));
        }
        taskTypesSequence.add(index++, taskType);
      }
    }
    index = isRf1 ? index + 1 : index + 2;
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
    taskParams.universeUUID = defaultUniverse.universeUUID;
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
