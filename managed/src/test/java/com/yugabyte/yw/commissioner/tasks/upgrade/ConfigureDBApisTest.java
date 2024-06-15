// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class ConfigureDBApisTest extends UpgradeTaskTest {

  @InjectMocks ConfigureDBApis configureDBApis;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    configureDBApis.setUserTaskUUID(UUID.randomUUID());
    setCheckNodesAreSafeToTakeDown(mockClient);
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
  }

  @Test
  public void testEnableDbApis() {
    ConfigureDBApiParams params = new ConfigureDBApiParams();
    params.enableYSQLAuth = true;
    params.enableYSQL = true;
    params.enableYCQL = true;
    params.enableYCQLAuth = true;
    params.ysqlPassword = "foo";
    params.ycqlPassword = "foo";
    TaskInfo taskInfo = submitTask(params, TaskType.ConfigureDBApis, commissioner);
    assertEquals(Success, taskInfo.getTaskState());
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .task(TaskType.UpdateNodeDetails)
        .applyRound()
        .addTasks(TaskType.UpdateUniverseCommunicationPorts)
        .addTasks(TaskType.UpdateClusterAPIDetails)
        .addTasks(TaskType.ChangeAdminPassword)
        .addTasks(TaskType.ChangeAdminPassword)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDisableDbApis() {
    ConfigureDBApiParams params = new ConfigureDBApiParams();
    params.enableYSQLAuth = false;
    params.enableYSQL = false;
    params.enableYCQL = false;
    params.enableYCQLAuth = false;
    params.ysqlPassword = "foo";
    params.ycqlPassword = "foo";
    TaskInfo taskInfo = submitTask(params, TaskType.ConfigureDBApis, commissioner);
    assertEquals(Success, taskInfo.getTaskState());
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.ChangeAdminPassword)
        .addTasks(TaskType.ChangeAdminPassword)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .task(TaskType.UpdateNodeDetails)
        .applyRound()
        .addTasks(TaskType.UpdateUniverseCommunicationPorts)
        .addTasks(TaskType.UpdateClusterAPIDetails)
        .verifyTasks(taskInfo.getSubTasks());
  }

  private MockUpgrade initMockUpgrade() {
    MockUpgrade mockUpgrade = initMockUpgrade(ConfigureDBApis.class);
    mockUpgrade.setUpgradeContext(UpgradeTaskBase.RUN_BEFORE_STOPPING);
    return mockUpgrade;
  }
}
