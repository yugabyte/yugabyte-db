// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.contains;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.ExportTelemetryConfig;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.YSQLQueryLogConfig;
import java.util.concurrent.TimeoutException;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.Silent.class)
public class ConfigureExportTelemetryConfigTest extends UpgradeTaskTest {

  private static final String USER_PG_CONF_CSV = "\"log_line_prefix='%m user '\"";
  private static final String SAVED_QUERY_LOG_PREFIX = "%m [%p] saved ";
  private static final String QUERY_LOG_PREFIX = "%m [%p] api ";

  private int expectedUniverseVersion = 2;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    try {
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
      setCheckNodesAreSafeToTakeDown(mockClient);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }

    // A universe set up before the query log prefix won over the user gflag: the export is on,
    // and the user ysql_pg_conf_csv sets a different log_line_prefix.
    QueryLogConfig savedQueryLogConfig = buildQueryLogConfig(SAVED_QUERY_LOG_PREFIX);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of(),
                      ImmutableMap.of(GFlagsUtil.YSQL_PG_CONF_CSV, USER_PG_CONF_CSV));
              userIntent.queryLogConfig = savedQueryLogConfig;
              // ManageOtelCollector builds its install payload from the universe architecture.
              universe.getUniverseDetails().arch = Architecture.x86_64;
            });
    // With an architecture set, the precheck looks for a local release package the test lacks.
    RuntimeConfigEntry.upsert(
        defaultUniverse, UniverseConfKeys.validateLocalRelease.getKey(), "false");
    expectedUniverseVersion++;
    ExportTelemetryConfig row = new ExportTelemetryConfig();
    row.setUniverseUuid(defaultUniverse.getUniverseUUID());
    row.setTelemetryConfig(TelemetryConfig.builder().queryLogConfig(savedQueryLogConfig).build());
    row.save();
  }

  private static QueryLogConfig buildQueryLogConfig(String logLinePrefix) {
    YSQLQueryLogConfig ysqlQueryLogConfig = new YSQLQueryLogConfig();
    ysqlQueryLogConfig.setEnabled(true);
    ysqlQueryLogConfig.setLogLinePrefix(logLinePrefix);
    QueryLogConfig queryLogConfig = new QueryLogConfig();
    queryLogConfig.setYsqlQueryLogConfig(ysqlQueryLogConfig);
    return queryLogConfig;
  }

  private TaskInfo submitTask(TelemetryConfig telemetryConfig, UpgradeOption upgradeOption) {
    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    params.clusters = defaultUniverse.getUniverseDetails().clusters;
    params.upgradeOption = upgradeOption;
    params.setTelemetryConfig(telemetryConfig);
    return submitTask(
        params, TaskType.ConfigureExportTelemetryConfig, commissioner, expectedUniverseVersion);
  }

  private TaskInfo submitQueryLogAndMetricsConfig(UpgradeOption upgradeOption) {
    return submitTask(
        TelemetryConfig.builder()
            .queryLogConfig(buildQueryLogConfig(QUERY_LOG_PREFIX))
            .metricsExportConfig(new MetricsExportConfig())
            .build(),
        upgradeOption);
  }

  @Test
  public void testNonRestartSetsYsqlPgConfCsvInMemory() throws Exception {
    TaskInfo taskInfo = submitQueryLogAndMetricsConfig(UpgradeOption.NON_RESTART_UPGRADE);
    assertEquals(Success, taskInfo.getTaskState());

    // The running masters and tservers get the requested query log prefix, which the gflags files
    // and the OTEL collector now use. The saved prefix differs, so this also checks that the
    // in-memory step reads the config after the task saves it.
    int numProcesses = defaultUniverse.getTServers().size() + defaultUniverse.getMasters().size();
    verify(mockClient, times(numProcesses))
        .setFlag(
            any(),
            eq(GFlagsUtil.YSQL_PG_CONF_CSV),
            contains("log_line_prefix='" + QUERY_LOG_PREFIX + "'"),
            eq(true));
    verify(mockClient, never())
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), contains("saved"), anyBoolean());
    verify(mockClient, never())
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), contains("%m user "), anyBoolean());
  }

  @Test
  public void testNonRestartInMemoryFailureDoesNotFailTask() throws Exception {
    // A hung process makes the RPC time out.
    when(mockClient.setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), anyString(), anyBoolean()))
        .thenThrow(new RuntimeException(new TimeoutException("setFlag timed out")));

    TaskInfo taskInfo = submitQueryLogAndMetricsConfig(UpgradeOption.NON_RESTART_UPGRADE);
    // The in-memory update is best effort: the next restart applies the gflags files.
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockClient, atLeastOnce())
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), anyString(), anyBoolean());
  }

  @Test
  public void testNonRestartSkipsNodesWithNoYsqlPgConfCsv() throws Exception {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.specificGFlags = null;
              userIntent.queryLogConfig = null;
            });
    expectedUniverseVersion++;
    ExportTelemetryConfig.getForUniverse(defaultUniverse.getUniverseUUID())
        .ifPresent(ExportTelemetryConfig::delete);

    TaskInfo taskInfo =
        submitTask(
            TelemetryConfig.builder().metricsExportConfig(new MetricsExportConfig()).build(),
            UpgradeOption.NON_RESTART_UPGRADE);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockClient, never())
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), anyString(), anyBoolean());
    assertTrue(
        taskInfo.getSubTasks().stream()
            .noneMatch(t -> t.getTaskType() == TaskType.SetFlagInMemory));
  }

  @Test
  public void testNonRestartUsesRequestedConfigToPickNodes() throws Exception {
    // Nothing is saved yet and there is no user value, so only the requested query log config
    // gives the nodes a value to set.
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.specificGFlags = null;
              userIntent.queryLogConfig = null;
            });
    expectedUniverseVersion++;
    ExportTelemetryConfig.getForUniverse(defaultUniverse.getUniverseUUID())
        .ifPresent(ExportTelemetryConfig::delete);

    TaskInfo taskInfo = submitQueryLogAndMetricsConfig(UpgradeOption.NON_RESTART_UPGRADE);
    assertEquals(Success, taskInfo.getTaskState());

    int numProcesses = defaultUniverse.getTServers().size() + defaultUniverse.getMasters().size();
    verify(mockClient, times(numProcesses))
        .setFlag(
            any(),
            eq(GFlagsUtil.YSQL_PG_CONF_CSV),
            contains("log_line_prefix='" + QUERY_LOG_PREFIX + "'"),
            eq(true));
  }

  @Test
  public void testNonRestartSkipsYsqlDisabledCluster() throws Exception {
    // With YSQL off, the gflags files get no telemetry entries, so there is nothing to set.
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.specificGFlags = null;
              userIntent.enableYSQL = false;
            });
    expectedUniverseVersion++;

    TaskInfo taskInfo = submitQueryLogAndMetricsConfig(UpgradeOption.NON_RESTART_UPGRADE);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockClient, never())
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), anyString(), anyBoolean());
    assertTrue(
        taskInfo.getSubTasks().stream()
            .noneMatch(t -> t.getTaskType() == TaskType.SetFlagInMemory));
  }

  @Test
  public void testRestartDoesNotSetYsqlPgConfCsvInMemory() throws Exception {
    TaskInfo taskInfo = submitQueryLogAndMetricsConfig(UpgradeOption.NON_ROLLING_UPGRADE);
    assertEquals(Success, taskInfo.getTaskState());

    // The restart applies the gflags files.
    verify(mockClient, never())
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), anyString(), anyBoolean());
  }
}
