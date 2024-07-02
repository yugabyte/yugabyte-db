// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class ThirdpartySoftwareUpgradeTest extends UpgradeTaskTest {

  @InjectMocks private ThirdpartySoftwareUpgrade thirdpartySoftwareUpgrade;

  private static final List<TaskType> TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.RunHooks,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl, // stop master
          TaskType.AnsibleClusterServerCtl, // stop tserver
          TaskType.AnsibleSetupServer, // reprovision
          TaskType.AnsibleConfigureServers, // reprovision
          TaskType.AnsibleConfigureServers, // gflags tserver
          TaskType.AnsibleConfigureServers, // gflags master
          TaskType.AnsibleClusterServerCtl, // start tserver
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.AnsibleClusterServerCtl, // start master
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.CheckFollowerLag,
          TaskType.RunHooks,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private int expectedUniverseVersion = 2;

  private List<Integer> nodesToFilter = new ArrayList<>();

  @Override
  @Before
  public void setUp() {
    super.setUp();
    attachHooks("ThirdpartySoftwareUpgrade");
    thirdpartySoftwareUpgrade.setUserTaskUUID(UUID.randomUUID());
    setCheckNodesAreSafeToTakeDown(mockClient);
    setFollowerLagMock();
  }

  private TaskInfo submitTask(ThirdpartySoftwareUpgradeParams requestParams, int version) {
    return submitTask(requestParams, TaskType.ThirdpartySoftwareUpgrade, commissioner, version);
  }

  private int assertSequence(Map<Integer, List<TaskInfo>> subTasksByPosition, int startPosition) {
    int position = startPosition;
    for (int nodeIdx : getRollingUpgradeNodeOrder(MASTER)) {
      String nodeName = String.format("host-n%d", nodeIdx);
      for (TaskType type : TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = tasks.get(0).getTaskType();

        assertEquals(1, tasks.size());
        assertEquals("At position " + position, type, taskType);
        if (!NON_NODE_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }
    return position;
  }

  @Test
  public void testInstanceReprovision() {
    testInstanceReprovision(false);
  }

  @Test
  public void testOnpremManualProvisionException() {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    onPremProvider.getDetails().skipProvisioning = true;
    onPremProvider.save();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        details -> {
          UniverseDefinitionTaskParams.UserIntent userIntent =
              details.getUniverseDetails().getPrimaryCluster().userIntent;
          userIntent.provider = onPremProvider.getUuid().toString();
          userIntent.providerType = Common.CloudType.onprem;
          userIntent.accessKeyCode = ApiUtils.DEFAULT_ACCESS_KEY_CODE;
        });
    expectedUniverseVersion++;
    ThirdpartySoftwareUpgradeParams taskParams = new ThirdpartySoftwareUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams, expectedUniverseVersion));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testOnpremOK() {
    AccessKey.create(
        gcpProvider.getUuid(), ApiUtils.DEFAULT_ACCESS_KEY_CODE, new AccessKey.KeyInfo());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        details -> {
          UniverseDefinitionTaskParams.UserIntent userIntent =
              details.getUniverseDetails().getPrimaryCluster().userIntent;
          userIntent.provider = onPremProvider.getUuid().toString();
          userIntent.providerType = Common.CloudType.onprem;
          userIntent.accessKeyCode = ApiUtils.DEFAULT_ACCESS_KEY_CODE;
        });
    expectedUniverseVersion++;
    testInstanceReprovision(true);
  }

  @Override
  protected List<Integer> getRollingUpgradeNodeOrder(UniverseTaskBase.ServerType serverType) {
    return super.getRollingUpgradeNodeOrder(serverType).stream()
        .filter(idx -> !nodesToFilter.contains(idx))
        .collect(Collectors.toList());
  }

  private void testInstanceReprovision(boolean forceAll) {
    ThirdpartySoftwareUpgradeParams taskParams = new ThirdpartySoftwareUpgradeParams();
    taskParams.setForceAll(forceAll);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;

    TaskInfo taskInfo = submitTask(taskParams, expectedUniverseVersion);

    int nodeCnt = getRollingUpgradeNodeOrder(MASTER).size();
    verify(mockNodeManager, times(13 * nodeCnt)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.CheckNodesAreSafeToTakeDown);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    // Assert that the first task is the pre-upgrade hooks
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);

    if (nodeCnt > 0) {
      assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList, position);
      position = assertSequence(subTasksByPosition, position);
    }
    // Assert that the first task is the pre-upgrade hooks
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunHooks);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateSucceeded, position);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList, position);

    assertEquals(subTasksByPosition.size(), position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testInstanceReprovisionRetries() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
    ThirdpartySoftwareUpgradeParams taskParams = new ThirdpartySoftwareUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.expectedUniverseVersion = -1;
    taskParams.creatingUser = defaultUser;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.ThirdpartySoftwareUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.ThirdpartySoftwareUpgrade,
        taskParams,
        false);
  }
}
