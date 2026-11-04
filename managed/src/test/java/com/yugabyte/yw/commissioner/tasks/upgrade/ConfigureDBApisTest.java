// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.ManagedLoadBalancerConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.MultiTenancyConfig;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
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

  // The collector's yugabyte scrape targets depend on the node's API flags and ports, so each
  // tserver must regenerate its collector config after UpdateUniverseFields changes them.
  @Test
  public void testReconfiguresOtelCollectorWhenMetricsExportEnabled() {
    // The collector config resolves the exporter UUID to a saved provider.
    DataDogConfig ddConfig = new DataDogConfig();
    ddConfig.setType(ProviderType.DATA_DOG);
    ddConfig.setSite("datadoghq.com");
    ddConfig.setApiKey("apiKey");
    TelemetryProvider telemetryProvider = new TelemetryProvider();
    telemetryProvider.setUuid(UUID.randomUUID());
    telemetryProvider.setCustomerUUID(defaultCustomer.getUuid());
    telemetryProvider.setName("dd-tp");
    telemetryProvider.setConfig(ddConfig);
    telemetryProvider.setTags(Map.of());
    telemetryProvider.save();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
          UniverseMetricsExporterConfig exporterConfig = new UniverseMetricsExporterConfig();
          exporterConfig.setExporterUuid(telemetryProvider.getUuid());
          metricsExportConfig.setUniverseMetricsExporterConfig(List.of(exporterConfig));
          u.getUniverseDetails().getPrimaryCluster().userIntent.metricsExportConfig =
              metricsExportConfig;
          u.getUniverseDetails().otelCollectorEnabled = true;
          // ManageOtelCollector builds its install payload from the universe architecture.
          u.getUniverseDetails().arch = Architecture.x86_64;
        });
    // With an architecture set, the precheck looks for a local release package the test lacks.
    RuntimeConfigEntry.upsert(
        defaultUniverse, UniverseConfKeys.validateLocalRelease.getKey(), "false");
    ConfigureDBApiParams params = new ConfigureDBApiParams();
    params.enableYSQLAuth = true;
    params.enableYSQL = true;
    params.enableYCQL = true;
    params.enableYCQLAuth = true;
    params.ysqlPassword = "foo";
    params.ycqlPassword = "foo";
    TaskInfo taskInfo = submitTask(params, TaskType.ConfigureDBApis, commissioner, -1);
    assertEquals(
        taskInfo.getErrorMessage()
            + taskInfo.getSubTasks().stream()
                .filter(t -> t.getTaskState() == Failure)
                .map(t -> t.getTaskType() + ": " + t.getErrorMessage())
                .collect(Collectors.toList()),
        Success,
        taskInfo.getTaskState());
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .tserverTasks(TaskType.UpdateUniverseFields, TaskType.ManageOtelCollector)
        .applyRound()
        .addTasks(TaskType.UpdateUniverseCommunicationPorts)
        .addTasks(TaskType.UpdateClusterAPIDetails)
        .addTasks(TaskType.ChangeAdminPassword)
        .addTasks(TaskType.ChangeAdminPassword)
        .verifyTasks(taskInfo.getSubTasks());
  }

  // The collector can run for log export alone. Without metrics export it has no scrape targets,
  // so ConfigureDBApis must not regenerate its config.
  @Test
  public void testSkipsOtelCollectorWhenMetricsExportDisabled() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(), u -> u.getUniverseDetails().otelCollectorEnabled = true);
    ConfigureDBApiParams params = new ConfigureDBApiParams();
    params.enableYSQLAuth = true;
    params.enableYSQL = true;
    params.enableYCQL = true;
    params.enableYCQLAuth = true;
    params.ysqlPassword = "foo";
    params.ycqlPassword = "foo";
    TaskInfo taskInfo = submitTask(params, TaskType.ConfigureDBApis, commissioner, -1);
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

  // The multi-tenancy restart rewrites each server's conf after the API step. It must keep the
  // API settings from this request, not the ones the universe had before it.
  @Test
  public void testMultiTenancyChangeKeepsNewYsqlAuth() {
    ConfigureDBApiParams params = new ConfigureDBApiParams();
    params.enableYSQLAuth = true;
    params.enableYSQL = true;
    params.ysqlPassword = "foo";
    MultiTenancyConfig mtConfig = new MultiTenancyConfig();
    mtConfig.setQosMaxDbCount(5);
    params.multiTenancy = mtConfig;
    // The controller copies the universe clusters into the params; the MT step reads them.
    params.clusters = defaultUniverse.getUniverseDetails().clusters;
    TaskInfo taskInfo = submitTask(params, TaskType.ConfigureDBApis, commissioner, -1);
    assertEquals(
        taskInfo.getErrorMessage()
            + taskInfo.getSubTasks().stream()
                .filter(t -> t.getTaskState() == Failure)
                .map(t -> t.getTaskType() + ": " + t.getErrorMessage())
                .collect(Collectors.toList()),
        Success,
        taskInfo.getTaskState());

    List<TaskInfo> mtConfigureTasks =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
            .filter(t -> t.getTaskParams().path("gflags").has(GFlagsUtil.QOS_MAX_DB_COUNT))
            .collect(Collectors.toList());
    assertFalse(mtConfigureTasks.isEmpty());
    for (TaskInfo task : mtConfigureTasks) {
      assertTrue(task.getTaskParams().get("enableYSQLAuth").asBoolean());
      assertTrue(task.getTaskParams().get("enableYSQL").asBoolean());
      assertFalse(task.getTaskParams().get("enableYCQL").asBoolean());
    }
  }

  private MockUpgrade initMockUpgrade() {
    MockUpgrade mockUpgrade = initMockUpgrade(ConfigureDBApis.class);
    mockUpgrade.setUpgradeContext(UpgradeTaskBase.RUN_BEFORE_STOPPING);
    return mockUpgrade;
  }
}
