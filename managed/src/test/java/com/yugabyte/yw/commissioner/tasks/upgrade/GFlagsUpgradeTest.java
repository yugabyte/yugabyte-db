// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class GFlagsUpgradeTest extends UpgradeTaskTest {

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
          TaskType.WaitForFollowerLag,
          TaskType.SetNodeState);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.WaitForFollowerLag,
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
  }

  private TaskInfo submitTask(GFlagsUpgradeParams requestParams) {
    return submitTask(requestParams, TaskType.GFlagsUpgrade, commissioner);
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
    int position = startPosition;
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
                  JsonNode gflagValue =
                      serverType.equals(MASTER)
                          ? Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}"))
                          : Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                  assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
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
                JsonNode gflagValue =
                    serverType.equals(MASTER)
                        ? Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}"))
                        : Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
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
                JsonNode gflagValue =
                    serverType.equals(MASTER)
                        ? Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}"))
                        : Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
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
    position =
        assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.NON_ROLLING_UPGRADE);
    position = assertCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(14, position);
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
    position =
        assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE_MASTER_ONLY, true);
    assertEquals(8, position);
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
    position =
        assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.FULL_UPGRADE_TSERVER_ONLY, true);
    assertEquals(8, position);
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
    position = assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.ROLLING_UPGRADE);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(29, position);
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
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
    position = assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.ROLLING_UPGRADE);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, true);
    assertEquals(39, position);
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
    position = assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.ROLLING_UPGRADE);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
    position = assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.ROLLING_UPGRADE);
    position = assertCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(66, position);
  }

  @Test
  public void testGFlagsUpgradeWithEmptyFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.version);
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
    Universe.saveDetails(defaultUniverse.universeUUID, updater);

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
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, false);
    position =
        assertSequence(subTasksByPosition, TSERVER, position, UpgradeOption.ROLLING_UPGRADE, true);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, true);
    assertEquals(39, position);
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
    Universe.saveDetails(defaultUniverse.universeUUID, updater);

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
    position =
        assertSequence(subTasksByPosition, MASTER, position, UpgradeOption.ROLLING_UPGRADE, true);
    position =
        assertCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(29, position);
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
      Universe.saveDetails(defaultUniverse.universeUUID, updater);

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
      assertEquals(serverType == MASTER ? 29 : 39, position);
    }
  }
}
