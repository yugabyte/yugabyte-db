// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.EITHER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
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
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListMastersResponse;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class ResizeNodeTest extends UpgradeTaskTest {

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
              userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
              userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
              universe
                  .getNodes()
                  .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE);
            });
    InstanceType.upsert(
        defaultProvider.uuid, NEW_INSTANCE_TYPE, 2.0, 8.0, new InstanceType.InstanceTypeDetails());
    InstanceType.upsert(
        defaultProvider.uuid,
        NEW_READ_ONLY_INSTANCE_TYPE,
        2.0,
        8.0,
        new InstanceType.InstanceTypeDetails());
    try {
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
    } catch (Exception ignored) {
    }
  }

  @Override
  protected PlacementInfo createPlacementInfo() {
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, placementInfo, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.uuid, placementInfo, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.uuid, placementInfo, 1, 2, false);
    return placementInfo;
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
  public void testChangingNumVolumesFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.clusters.get(0).userIntent.deviceInfo.volumeSize += 10;
    taskParams.clusters.get(0).userIntent.deviceInfo.numVolumes++;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString("Only volume size should be changed to do smart resize"));
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
        containsString("Only volume size should be changed to do smart resize"));
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
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertAllNodesActions(
            position,
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
    assertUniverseDataForReadReplicaClusters(true, true, true, true, 250, "c3.small");
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
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getReadOnlyClusters().get(0).userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getReadOnlyClusters().get(0).userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.getReadOnlyClusters().get(0).userIntent.providerType = Common.CloudType.aws;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true, false, true);
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
    assertEquals(1L, updateMounts.get(0).getPosition());
    assertTasksSequence(2, subTasks, true, true, true, false);
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
      boolean is_rf1) {
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    assertEquals(subTasks.size(), subTasksByPosition.size());
    int position = startPosition;
    if (startPosition == 0) {
      assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    }
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);

    position =
        assertTasksSequence(
            subTasksByPosition,
            EITHER,
            position,
            increaseVolume,
            changeInstance,
            waitForMasterLeader,
            is_rf1);
    position =
        assertTasksSequence(
            subTasksByPosition, TSERVER, position, increaseVolume, changeInstance, false, is_rf1);
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
      boolean is_rf1) {
    List<Integer> nodeIndexes =
        serverType == EITHER ? Arrays.asList(1, 3, 2) : Collections.singletonList(4);

    for (Integer nodeIndex : nodeIndexes) {
      String nodeName = String.format("host-n%d", nodeIndex);
      Map<Integer, Map<String, Object>> paramsForTask = new HashMap<>();
      List<TaskType> taskTypesSequence =
          is_rf1 ? new ArrayList<>(TASK_SEQUENCE_RF1) : new ArrayList<>(TASK_SEQUENCE);
      createTasksTypesForNode(
          serverType != EITHER,
          increaseVolume,
          changeInstance,
          taskTypesSequence,
          paramsForTask,
          waitForMasterLeader,
          is_rf1);

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
}
