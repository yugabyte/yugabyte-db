// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ArrayListMultimap;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Multimap;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class GFlagsUpgradeTest extends UpgradeTaskTest {

  private int expectedUniverseVersion = 2;

  @InjectMocks GFlagsUpgrade gFlagsUpgrade;

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.AnsibleConfigureServers,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState);

  private static final List<TaskType> NON_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState);

  private static final List<TaskType> NON_RESTART_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState);

  @Override
  @Before
  public void setUp() {
    super.setUp();
    gFlagsUpgrade.setUserTaskUUID(UUID.randomUUID());

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    try {
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private UUID addReadReplica() {
    UserIntent curIntent = defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.accessKeyCode = curIntent.accessKeyCode;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 2;

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    expectedUniverseVersion++;

    return defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid;
  }

  private TaskInfo submitTask(GFlagsUpgradeParams requestParams) {
    return submitTask(requestParams, TaskType.GFlagsUpgrade, commissioner, expectedUniverseVersion);
  }

  private TaskInfo submitTask(GFlagsUpgradeParams requestParams, int expectedVersion) {
    return submitTask(requestParams, TaskType.GFlagsUpgrade, commissioner, expectedVersion);
  }

  private int assertCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeType type,
      boolean isFinalStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();
    if (type.name().equals("ROLLING_UPGRADE_TSERVER_ONLY") && !isFinalStep) {
      commonNodeTasks.add(TaskType.ModifyBlackList);
    }
    if (isFinalStep) {
      commonNodeTasks.addAll(
          ImmutableList.of(TaskType.UpdateAndPersistGFlags, TaskType.UniverseUpdateSucceeded));
    }
    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }
    return position;
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeOption option) {
    return assertSequence(subTasksByPosition, serverType, startPosition, option, false);
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeOption option,
      boolean isEdit) {
    return assertSequence(subTasksByPosition, serverType, startPosition, option, isEdit, false);
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeOption option,
      boolean isEdit,
      boolean isDelete) {
    Map<String, String> gflags =
        serverType == MASTER
            ? ImmutableMap.of("master-flag", isEdit ? "m2" : "m1")
            : ImmutableMap.of("tserver-flag", isEdit ? "t2" : "t1");
    return assertSequence(subTasksByPosition, serverType, startPosition, option, isDelete, gflags);
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeOption option,
      boolean isDelete,
      Map<String, String> gflags) {
    int position = startPosition;
    ObjectNode gflagsJson = Json.newObject();
    gflags.forEach(
        (k, v) -> {
          gflagsJson.put(k, v);
        });
    switch (option) {
      case ROLLING_UPGRADE:
        List<TaskType> taskSequence =
            serverType == MASTER
                ? ROLLING_UPGRADE_TASK_SEQUENCE_MASTER
                : ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER;
        List<Integer> nodeOrder = getRollingUpgradeNodeOrder(serverType);
        for (int nodeIdx : nodeOrder) {
          String nodeName = String.format("host-n%d", nodeIdx);
          for (TaskType type : taskSequence) {
            List<TaskInfo> tasks = subTasksByPosition.get(position);
            TaskType taskType = tasks.get(0).getTaskType();
            assertEquals(1, tasks.size());
            assertEquals(type, taskType);
            if (!NON_NODE_TASKS.contains(taskType)) {
              Map<String, Object> assertValues =
                  new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

              if (taskType.equals(TaskType.AnsibleConfigureServers)) {
                if (!isDelete) {
                  assertValues.putAll(ImmutableMap.of("gflags", gflagsJson));
                }
              }
              assertNodeSubTask(tasks, assertValues);
            }
            position++;
          }
        }
        break;
      case NON_ROLLING_UPGRADE:
        for (TaskType type : NON_ROLLING_UPGRADE_TASK_SEQUENCE) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = assertTaskType(tasks, type);

          if (NON_NODE_TASKS.contains(taskType)) {
            assertEquals(1, tasks.size());
          } else {
            Map<String, Object> assertValues =
                new HashMap<>(
                    ImmutableMap.of(
                        "nodeNames",
                        (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                        "nodeCount",
                        3));
            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              if (!isDelete) {
                assertValues.putAll(ImmutableMap.of("gflags", gflagsJson));
              }
              assertValues.put("processType", serverType.toString());
            }
            assertEquals(3, tasks.size());
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
        break;
      case NON_RESTART_UPGRADE:
        for (TaskType type : NON_RESTART_UPGRADE_TASK_SEQUENCE) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = assertTaskType(tasks, type);

          if (NON_NODE_TASKS.contains(taskType)) {
            assertEquals(1, tasks.size());
          } else {
            Map<String, Object> assertValues =
                new HashMap<>(
                    ImmutableMap.of(
                        "nodeNames",
                        (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                        "nodeCount",
                        3));
            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              if (!isDelete) {
                assertValues.putAll(ImmutableMap.of("gflags", gflagsJson));
              }
            }
            assertEquals(3, tasks.size());
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
        break;
    }

    return position;
  }

  @Test
  public void testGFlagsNonRollingUpgrade() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.NON_ROLLING_UPGRADE);
    position = assertCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(15, position);
  }

  @Test
  public void testGFlagsNonRollingMasterOnlyUpgrade() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE_MASTER_ONLY, true);
    assertEquals(9, position);
  }

  @Test
  public void testGFlagsNonRollingTServerOnlyUpgrade() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.FULL_UPGRADE_TSERVER_ONLY, true);
    assertEquals(9, position);
  }

  @Test
  public void testGFlagsUpgradeWithMasterGFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position = assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.ROLLING_UPGRADE);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(30, position);
  }

  @Test
  public void testGFlagsUpgradeWithTServerGFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    ArgumentCaptor<NodeTaskParams> commandParams = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, times(9)).nodeCommand(any(), commandParams.capture());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
    position = assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.ROLLING_UPGRADE);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, true);
    assertEquals(43, position);
  }

  @Test
  public void testGFlagsUpgrade() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position = assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.ROLLING_UPGRADE);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
    position = assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.ROLLING_UPGRADE);
    position = assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(70, position);
  }

  @Test
  public void testGFlagsUpgradeWithEmptyFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
  }

  @Test
  public void testGFlagsUpgradeWithSameMasterFlags() {

    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> masterFlags = ImmutableMap.of("master-flag", "m123");
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = masterFlags;
          userIntent.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = masterFlags;
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t2");

    TaskInfo taskInfo = submitTask(taskParams, 3);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    subTasksByPosition.get(0);
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
    position =
        assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.ROLLING_UPGRADE, true);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, true);
    assertEquals(43, position);
  }

  @Test
  public void testGFlagsUpgradeWithSameTserverFlags() {

    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "m123");
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = ImmutableMap.of("master-flag", "m1");
          userIntent.tserverGFlags = tserverFlags;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m2");
    taskParams.tserverGFlags = tserverFlags;

    TaskInfo taskInfo = submitTask(taskParams, 3);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.ROLLING_UPGRADE, true);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(30, position);
  }

  @Test
  public void testRemoveFlags() {
    for (ServerType serverType : ImmutableList.of(MASTER, TSERVER)) {
      // Simulate universe created with master flags and tserver flags.
      final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "t1");
      final Map<String, String> masterGFlags = ImmutableMap.of("master-flag", "m1");
      Universe.UniverseUpdater updater =
          universe -> {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
            userIntent.masterGFlags = masterGFlags;
            userIntent.tserverGFlags = tserverFlags;
            universe.setUniverseDetails(universeDetails);
          };
      Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

      GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
      // This is a delete operation on the master flags.
      if (serverType == MASTER) {
        taskParams.masterGFlags = new HashMap<>();
        taskParams.tserverGFlags = tserverFlags;
      } else {
        taskParams.masterGFlags = masterGFlags;
        taskParams.tserverGFlags = new HashMap<>();
      }

      int expectedVersion = serverType == MASTER ? 3 : 5;
      TaskInfo taskInfo = submitTask(taskParams, expectedVersion);
      assertEquals(Success, taskInfo.getTaskState());

      int numInvocations = serverType == MASTER ? 9 : 18;
      verify(mockNodeManager, times(numInvocations)).nodeCommand(any(), any());

      List<TaskInfo> subTasks = new ArrayList<>(taskInfo.getSubTasks());
      Map<Integer, List<TaskInfo>> subTasksByPosition =
          subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
      int position = 0;
      assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
      if (serverType != MASTER) {
        position =
            assertCommonTasks(
                subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
      }
      position =
          assertSequence(
              subTasksByPosition, serverType, position, UpgradeOption.ROLLING_UPGRADE, true, true);
      position =
          assertCommonTasks(
              subTasksByPosition,
              position,
              serverType == MASTER
                  ? UpgradeType.ROLLING_UPGRADE_MASTER_ONLY
                  : UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY,
              true);
      assertEquals(serverType == MASTER ? 30 : 43, position);
    }
  }

  @Test
  public void testGflagChangingIntent() {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Verify enabled by default.
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, true);
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    Map<String, String> gflags = ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false");

    // this will cause both processes to be updated
    taskParams.masterGFlags = gflags;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertSequence(
            subTasksByPosition, MASTER, position, UpgradeOption.ROLLING_UPGRADE, false, gflags);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
    position =
        assertSequence(
            subTasksByPosition,
            TSERVER,
            position,
            UpgradeOption.ROLLING_UPGRADE,
            false,
            Collections.emptyMap());
    position = assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);

    universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Verify changed to false.
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, false);
  }

  @Test
  public void testGflagsConflictingWithDefault() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    Map<String, String> gflags = ImmutableMap.of(GFlagsUtil.MASTER_ADDRESSES, "1.1.33.666:74350");
    // this will cause both processes to be updated
    taskParams.masterGFlags = gflags;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
        containsString(
            "Node host-n1: value 1.1.33.666:74350 for master_addresses is conflicting"
                + " with autogenerated value 10.0.0.1:7100,10.0.0.2:7100,10.0.0.3:7100"));

    // No conflicts here as tserver does not have such gflag.
    taskParams.masterGFlags = new HashMap<>();
    taskParams.tserverGFlags = gflags;
    taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testContradictoryGFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    // this will cause both processes to be updated
    taskParams.masterGFlags = ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false");
    taskParams.tserverGFlags = ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "true");
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString(
            "G-Flag value for 'enable_ysql' is inconsistent "
                + "between master and tserver ('false' vs 'true')"));
  }

  @Test
  public void testGFlagsUpgradeForRR() {
    UUID clusterId = addReadReplica();

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            (u) -> {
              u.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  new SpecificGFlags();
            });
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags roGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1"), ImmutableMap.of("tserver-flag", "t1"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getReadOnlyClusters().get(0).userIntent.specificGFlags = roGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    Set<String> nodeNames = new HashSet<>();
    defaultUniverse
        .getUniverseDetails()
        .getNodesInCluster(clusterId)
        .forEach(node -> nodeNames.add(node.getNodeName()));

    Set<String> changedNodes = new HashSet<>();
    subTasks.stream()
        .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
        .map(t -> t.getDetails().get("nodeName").asText())
        .forEach(changedNodes::add);
    assertEquals(nodeNames, changedNodes);

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        new SpecificGFlags(),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags);
    assertEquals(
        roGFlags,
        defaultUniverse
            .getUniverseDetails()
            .getReadOnlyClusters()
            .get(0)
            .userIntent
            .specificGFlags);
    assertEquals(
        roGFlags.getGFlags(null, MASTER),
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.masterGFlags);
    assertEquals(
        roGFlags.getGFlags(null, TSERVER),
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.tserverGFlags);
  }

  @Test
  public void testGFlagsUpgradeForRRStartInheriting() throws Exception {
    UUID clusterId = addReadReplica();

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      Collections.emptyMap(), ImmutableMap.of("tserver-flag", "t1"));

              UniverseDefinitionTaskParams.Cluster rr =
                  universe.getUniverseDetails().getReadOnlyClusters().get(0);
              rr.userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master-flag", "rm1"),
                      ImmutableMap.of("tserver-flag", "rt1", "tserver-flag2", "t2"));
            });
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    SpecificGFlags roGFlags = new SpecificGFlags();
    roGFlags.setInheritFromPrimary(true);
    taskParams.getReadOnlyClusters().get(0).userIntent.specificGFlags = roGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    try {
      Set<String> nodeNames = new HashSet<>();
      defaultUniverse
          .getUniverseDetails()
          .getNodesInCluster(clusterId)
          .forEach(node -> nodeNames.add(node.getNodeName()));

      Set<String> changedNodes = new HashSet<>();
      subTasks.stream()
          .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
          .map(t -> t.getDetails().get("nodeName").asText())
          .forEach(changedNodes::add);
      assertEquals(nodeNames, changedNodes);

      assertEquals(
          new HashMap<>(Collections.singletonMap("tserver-flag", "t1")),
          getGflagsForNode(subTasks, changedNodes.iterator().next(), TSERVER));

      assertEquals(
          Collections.singleton("tserver-flag2"),
          getGflagsToRemoveForNode(subTasks, changedNodes.iterator().next(), TSERVER));

      defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      assertEquals(
          roGFlags,
          defaultUniverse
              .getUniverseDetails()
              .getReadOnlyClusters()
              .get(0)
              .userIntent
              .specificGFlags);
      SpecificGFlags primarySpecificGFlags =
          defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags;
      assertEquals(
          primarySpecificGFlags.getGFlags(null, MASTER),
          defaultUniverse
              .getUniverseDetails()
              .getReadOnlyClusters()
              .get(0)
              .userIntent
              .masterGFlags);
      assertEquals(
          primarySpecificGFlags.getGFlags(null, TSERVER),
          defaultUniverse
              .getUniverseDetails()
              .getReadOnlyClusters()
              .get(0)
              .userIntent
              .tserverGFlags);
    } catch (Exception e) {
      log.error("", e);
      throw e;
    }
  }

  @Test
  public void testGFlagsUpgradePrimaryWithRR() {
    addReadReplica();

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1"), ImmutableMap.of("tserver-flag", "t1"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    Set<String> changedNodes = new HashSet<>();
    subTasks.stream()
        .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
        .map(t -> t.getDetails().get("nodeName").asText())
        .forEach(changedNodes::add);
    assertEquals(6, changedNodes.size()); // all nodes are affected

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertNull(
        defaultUniverse
            .getUniverseDetails()
            .getReadOnlyClusters()
            .get(0)
            .userIntent
            .specificGFlags);
    assertEquals(
        specificGFlags,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags);
    assertEquals(
        specificGFlags.getGFlags(null, MASTER),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags);
    assertEquals(
        specificGFlags.getGFlags(null, TSERVER),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags);
  }

  @Test
  public void testContradictoryGFlagsNewVersion() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "true"),
            ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString(
            "G-Flag value for 'enable_ysql' is inconsistent between master and tserver ('true' vs"
                + " 'false')"));
  }

  @Test
  public void testSpecificGFlagsNoChanges() {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              SpecificGFlags specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master-flag", "m"), ImmutableMap.of("tserver-flag", "t1"));
              universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  specificGFlags;
            });
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString("No changes in gflags (modify specificGflags in cluster)"));
  }

  @Test
  public void testGflagsDeletedErrorNewVersion() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          SpecificGFlags specificGFlags =
              SpecificGFlags.construct(
                  ImmutableMap.of("master-flag", "m"), ImmutableMap.of("tserver-flag", "t1"));
          universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
              specificGFlags;
        });
    expectedUniverseVersion++;

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master-flag", "m2"), ImmutableMap.of("a", "b"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString("Cannot delete gFlags through non-restart upgrade option."));
  }

  @Test
  public void testPerAZGflags() {
    List<UUID> azList = Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid());
    AtomicInteger idx = new AtomicInteger();
    Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          universe
              .getUniverseDetails()
              .nodeDetailsSet
              .forEach(
                  node -> {
                    node.azUuid = azList.get(idx.getAndIncrement());
                  });
        });
    addReadReplica();
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1"), ImmutableMap.of("tserver-flag", "t1"));
    Map<UUID, SpecificGFlags.PerProcessFlags> perAZ = new HashMap<>();
    specificGFlags.setPerAZ(perAZ);

    // First -> redefining existing
    UUID firstAZ = azList.get(0);
    SpecificGFlags.PerProcessFlags firstPerProcessFlags = new SpecificGFlags.PerProcessFlags();
    firstPerProcessFlags.value =
        ImmutableMap.of(
            MASTER, ImmutableMap.of("master-flag", "m2"),
            TSERVER, ImmutableMap.of("tserver-flag", "t2"));
    perAZ.put(firstAZ, firstPerProcessFlags);

    // Second -> adding new
    UUID secondAZ = azList.get(1);
    SpecificGFlags.PerProcessFlags secondPerProcessFlags = new SpecificGFlags.PerProcessFlags();
    secondPerProcessFlags.value =
        ImmutableMap.of(
            MASTER, ImmutableMap.of("master-flag2", "m2"),
            TSERVER, ImmutableMap.of("tserver-flag2", "t2"));
    perAZ.put(secondAZ, secondPerProcessFlags);

    // Third -> no changes
    UUID thirdAZ = azList.get(2);
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    taskParams.getReadOnlyClusters().get(0).userIntent.specificGFlags =
        SpecificGFlags.constructInherited();

    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());

    Multimap<UUID, String> nodeNamesByUUID = ArrayListMultimap.create();
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      nodeNamesByUUID.put(nodeDetails.getAzUuid(), nodeDetails.nodeName);
    }
    assertEquals(6, nodeNamesByUUID.size());
    assertEquals(3, nodeNamesByUUID.keySet().size());

    for (String first : nodeNamesByUUID.get(firstAZ)) {
      assertEquals(
          new HashMap<>(Collections.singletonMap("tserver-flag", "t2")),
          getGflagsForNode(subTasks, first, TSERVER));
      if (defaultUniverse.getNode(first).isMaster) {
        assertEquals(
            new HashMap<>(Collections.singletonMap("master-flag", "m2")),
            getGflagsForNode(subTasks, first, MASTER));
      }
    }
    for (String second : nodeNamesByUUID.get(secondAZ)) {
      assertEquals(
          new HashMap<>(ImmutableMap.of("tserver-flag", "t1", "tserver-flag2", "t2")),
          getGflagsForNode(subTasks, second, TSERVER));
      if (defaultUniverse.getNode(second).isMaster) {
        assertEquals(
            new HashMap<>(ImmutableMap.of("master-flag", "m1", "master-flag2", "m2")),
            getGflagsForNode(subTasks, second, MASTER));
      }
    }
    for (String third : nodeNamesByUUID.get(thirdAZ)) {
      assertEquals(
          new HashMap<>(Collections.singletonMap("tserver-flag", "t1")),
          getGflagsForNode(subTasks, third, TSERVER));
      if (defaultUniverse.getNode(third).isMaster) {
        assertEquals(
            new HashMap<>(Collections.singletonMap("master-flag", "m1")),
            getGflagsForNode(subTasks, third, MASTER));
      }
    }
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    SpecificGFlags readonlyGflags =
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.specificGFlags;
    assertEquals(specificGFlags.getPerProcessFlags(), readonlyGflags.getPerProcessFlags());
    assertEquals(specificGFlags.getPerAZ(), readonlyGflags.getPerAZ());
  }

  @Test
  public void testSpecificGflagChangingIntent() {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Verify enabled by default.
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, true);
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false"), Collections.emptyMap());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    position =
        assertSequence(
            subTasksByPosition,
            MASTER,
            position,
            UpgradeOption.ROLLING_UPGRADE,
            false,
            specificGFlags.getGFlags(null, MASTER));
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
    position =
        assertSequence(
            subTasksByPosition,
            TSERVER,
            position,
            UpgradeOption.ROLLING_UPGRADE,
            false,
            Collections.emptyMap());
    position = assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);

    universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Verify changed to false.
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, false);
    assertEquals(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags,
        specificGFlags);
  }

  @Test
  public void testUpgradeAutoFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          SpecificGFlags specificGFlags =
              SpecificGFlags.construct(ImmutableMap.of("master-flag", "m1"), ImmutableMap.of());
          universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
              specificGFlags;
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
              "2.18.2.0-b65";
        });
    expectedUniverseVersion++;

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master-flag", "m2"), ImmutableMap.of());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    Universe xClusterUniverse = ModelFactory.createUniverse("univ-2");
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            "test-1", defaultUniverse.getUniverseUUID(), xClusterUniverse.getUniverseUUID());
    xClusterConfig.updateStatus(XClusterConfig.XClusterConfigStatusType.Running);
    TestHelper.updateUniverseVersion(xClusterUniverse, "2.14.0.0-b1");

    try {
      when(mockGFlagsValidation.getFilteredAutoFlagsWithNonInitialValue(
              anyMap(), anyString(), any()))
          .thenReturn(ImmutableMap.of("master-flag", "m2"));
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      GFlagsValidation.AutoFlagDetails flag = new GFlagsValidation.AutoFlagDetails();
      flag.name = "master-flag-2";
      autoFlagsPerServer.autoFlagDetails = Collections.singletonList(flag);
      GFlagsValidation.AutoFlagDetails flag2 = new GFlagsValidation.AutoFlagDetails();
      flag2.name = "master-flag";
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer2 =
          new GFlagsValidation.AutoFlagsPerServer();
      autoFlagsPerServer2.autoFlagDetails = Collections.singletonList(flag2);
      when(mockGFlagsValidation.extractAutoFlags(anyString(), (ServerType) any()))
          .thenReturn(autoFlagsPerServer)
          .thenReturn(autoFlagsPerServer2);
    } catch (IOException e) {
      fail();
    }

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
        containsString(
            "Cannot upgrade auto flags as an XCluster linked universe "
                + xClusterUniverse.getUniverseUUID()
                + " does not support auto flags"));

    TestHelper.updateUniverseVersion(xClusterUniverse, "2.17.0.0-b1");
    taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
        containsString(
            "master-flag is not present in the xCluster linked universe: "
                + xClusterUniverse.getUniverseUUID()));

    taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testGFlagsUpgradeNonRestart() {
    addReadReplica();

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1111"), ImmutableMap.of("tserver-flag", "t1111"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    taskParams.upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    subTasks.stream()
        .filter(t -> t.getTaskType() == TaskType.SetFlagInMemory)
        .forEach(
            task -> {
              ServerType process = ServerType.valueOf(task.getDetails().get("serverType").asText());
              Map<String, String> gflags =
                  Json.fromJson(task.getDetails().get("gflags"), Map.class);
              if (process == MASTER) {
                assertEquals(specificGFlags.getGFlags(null, MASTER), gflags);
              } else {
                assertEquals(specificGFlags.getGFlags(null, TSERVER), gflags);
              }
            });
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        specificGFlags,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags);
    assertEquals(
        specificGFlags.getGFlags(null, MASTER),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags);
    assertEquals(
        specificGFlags.getGFlags(null, TSERVER),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags);
  }

  @Test
  public void testGFlagsUpgradePrimaryWithRRRetries() {
    addReadReplica();

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1"), ImmutableMap.of("tserver-flag", "t1"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.GFlagsUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.GFlagsUpgrade,
        taskParams,
        false);
  }

  private Map<String, String> getGflagsForNode(
      List<TaskInfo> tasks, String nodeName, ServerType process) {
    TaskInfo taskInfo = findGflagsTask(tasks, nodeName, process);
    JsonNode gflags = taskInfo.getDetails().get("gflags");
    return Json.fromJson(gflags, Map.class);
  }

  private Set<String> getGflagsToRemoveForNode(
      List<TaskInfo> tasks, String nodeName, ServerType process) {
    TaskInfo taskInfo = findGflagsTask(tasks, nodeName, process);
    ArrayNode gflagsToRemove = (ArrayNode) taskInfo.getDetails().get("gflagsToRemove");
    return Json.fromJson(gflagsToRemove, Set.class);
  }

  private TaskInfo findGflagsTask(List<TaskInfo> tasks, String nodeName, ServerType process) {
    return tasks.stream()
        .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
        .filter(t -> t.getDetails().get("nodeName").asText().equals(nodeName))
        .filter(
            t ->
                t.getDetails()
                    .get("properties")
                    .get("processType")
                    .asText()
                    .equals(process.toString()))
        .findFirst()
        .get();
  }
}
