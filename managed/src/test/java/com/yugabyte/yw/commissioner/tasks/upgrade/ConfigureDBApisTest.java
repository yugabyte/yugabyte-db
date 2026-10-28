// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.ManagedLoadBalancerConfig;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
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
public class ConfigureDBApisTest extends UpgradeTaskTest {

  @InjectMocks ConfigureDBApis configureDBApis;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    configureDBApis.setUserTaskUUID(UUID.randomUUID());
    attachHooks("ConfigureDBApis");
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
        .tserverTask(TaskType.UpdateUniverseFields)
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
        .addTasks(TaskType.DropTable, TaskType.DropTable, TaskType.DropTable)
        .addTasks(TaskType.ChangeAdminPassword)
        .addTasks(TaskType.ChangeAdminPassword)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .tserverTask(TaskType.UpdateUniverseFields)
        .applyRound()
        .addTasks(TaskType.UpdateUniverseCommunicationPorts)
        .addTasks(TaskType.UpdateClusterAPIDetails)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testPortChangeReconcilesManagedLoadBalancerLastWithNewPorts() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          ManagedLoadBalancerConfig config = new ManagedLoadBalancerConfig();
          config.setEnablePrivate(true);
          u.getUniverseDetails().getPrimaryCluster().userIntent.setManagedLoadBalancer(config);
        });
    ConfigureDBApiParams params = new ConfigureDBApiParams();
    params.enableYSQLAuth = true;
    params.enableYSQL = true;
    params.enableYCQL = true;
    params.enableYCQLAuth = true;
    params.ysqlPassword = "foo";
    params.ycqlPassword = "foo";
    params.communicationPorts.ysqlServerRpcPort = 5434;

    TaskInfo taskInfo = submitTask(params, TaskType.ConfigureDBApis, commissioner, -1);

    assertEquals(
        taskInfo.getErrorMessage()
            + taskInfo.getSubTasks().stream()
                .filter(t -> t.getTaskState() == Failure)
                .map(t -> t.getTaskType() + ": " + t.getErrorMessage())
                .collect(Collectors.toList()),
        Success,
        taskInfo.getTaskState());
    // After the passwords: a load balancer error must not leave auth on with the default password.
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .tserverTask(TaskType.UpdateUniverseFields)
        .applyRound()
        .addTasks(TaskType.UpdateUniverseCommunicationPorts)
        .addTasks(TaskType.UpdateClusterAPIDetails)
        .addTasks(TaskType.ChangeAdminPassword)
        .addTasks(TaskType.ChangeAdminPassword)
        .addTasks(TaskType.EnsureManagedLoadBalancer)
        .addTasks(TaskType.ManageLoadBalancerGroup)
        .verifyTasks(taskInfo.getSubTasks());
    // The subtask reads the ports that UpdateUniverseCommunicationPorts stored.
    verify(cloudAPI)
        .ensureManagedLoadBalancer(
            any(), eq("region-1"), any(), any(), eq(List.of(5434, 9042)), any());
  }

  private MockUpgrade initMockUpgrade() {
    MockUpgrade mockUpgrade = initMockUpgrade(ConfigureDBApis.class);
    mockUpgrade.setUpgradeContext(UpgradeTaskBase.RUN_BEFORE_STOPPING);
    return mockUpgrade;
  }
}
