// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BiConsumer;
import java.util.stream.Collectors;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RebootUniverseTest extends UpgradeTaskTest {

  @InjectMocks private RebootUniverse rebootUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    attachHooks("RebootUniverse");
    rebootUniverse.setUserTaskUUID(UUID.randomUUID());
    setCheckNodesAreSafeToTakeDown(mockClient);
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
  }

  private TaskInfo submitTask(UpgradeTaskParams requestParams) {
    return submitTask(requestParams, TaskType.RebootUniverse, commissioner);
  }

  @Test
  public void testNonRollingReboot() {
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  public void tesNonRestartUpgradeReboot() {
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    taskParams.upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  public void testRollingReboot() {
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockNodeManager, times(27)).nodeCommand(any(), any());
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE, true)
        .tserverTask(TaskType.RebootServer, null)
        .applyRound()
        .verifyTasks(taskInfo.getSubTasks());
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(RebootUniverse.class);
  }

  @Test
  public void testRollingRebootNotLiveNode() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.nodeDetailsSet.iterator().next().state = NodeDetails.NodeState.UpdateGFlags;
          u.setUniverseDetails(details);
        },
        false);
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockNodeManager, times(27)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    for (int i = 0; i < subTasks.size(); i++) {
      if (subTasksByPosition.get(i).get(0).getTaskType() == TaskType.FreezeUniverse) {
        break;
      }
      // Verify no CheckNodesAreSafeToTakeDown before freeze
      assertNotEquals(
          TaskType.CheckNodesAreSafeToTakeDown, subTasksByPosition.get(i).get(0).getTaskType());
    }
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testRollingRebootRetries() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.creatingUser = defaultUser;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RebootUniverse,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RebootUniverse,
        taskParams,
        false);
  }

  @Test
  public void testRollingRebootNodeOrder() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          Set<NodeDetails> detailsSet = new HashSet<>();
          AtomicInteger idx = new AtomicInteger();
          BiConsumer<AvailabilityZone, Boolean> nodeCreator =
              (az, isMaster) -> {
                NodeDetails node = ApiUtils.getDummyNodeDetails(idx.incrementAndGet());
                node.isMaster = isMaster;
                node.azUuid = az.getUuid();
                node.placementUuid = details.getPrimaryCluster().uuid;
                detailsSet.add(node);
              };

          nodeCreator.accept(az1, true); // 1
          nodeCreator.accept(az2, true); // 2
          nodeCreator.accept(az1, true); // 3
          nodeCreator.accept(az3, true); // 4
          nodeCreator.accept(az3, false); // 5
          nodeCreator.accept(az3, false); // 6
          nodeCreator.accept(az2, false); // 7
          nodeCreator.accept(az1, false); // 8

          details.nodeDetailsSet = detailsSet;
          u.setUniverseDetails(details);
        },
        false);
    UpgradeTaskParams taskParams = new UpgradeTaskParams();
    taskParams.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    List<String> expected =
        Arrays.asList(
            // masters first
            "host-n1",
            "host-n3",
            "host-n4",
            "host-n2", // leader
            "host-n7",
            "host-n8",
            "host-n5",
            "host-n6");

    List<String> nodeNames = new ArrayList<>();
    for (TaskInfo subTask : subTasks) {
      if (subTask.getTaskType() == TaskType.RebootServer) {
        nodeNames.add(subTask.getTaskParams().get("nodeName").textValue());
      }
    }
    Assert.assertEquals(expected, nodeNames);
  }
}
