// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RestartUniverseTest extends UpgradeTaskTest {

  @InjectMocks private RestartUniverse restartUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    setCheckNodesAreSafeToTakeDown(mockClient);
    restartUniverse.setUserTaskUUID(UUID.randomUUID());
    attachHooks("RestartUniverse");

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
  }

  private TaskInfo submitTask(RestartTaskParams requestParams) {
    return submitTask(requestParams, TaskType.RestartUniverse, commissioner);
  }

  @Test
  public void testRollingRestart() {
    RestartTaskParams taskParams = new RestartTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .applyRound()
        .verifyTasks(taskInfo.getSubTasks());
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testRollingRestartRetries() {
    RestartTaskParams taskParams = new RestartTaskParams();
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.creatingUser = defaultUser;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RestartUniverse,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RestartUniverse,
        taskParams,
        false);
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(RestartUniverse.class);
  }
}
