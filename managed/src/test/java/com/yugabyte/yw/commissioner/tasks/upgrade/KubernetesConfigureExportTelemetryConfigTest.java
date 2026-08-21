// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YSQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.YSQLQueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.YSQLQueryLogConfig.YSQLLogErrorVerbosity;
import com.yugabyte.yw.models.helpers.exporters.query.YSQLQueryLogConfig.YSQLLogStatement;
import com.yugabyte.yw.models.helpers.exporters.query.YSQLQueryLogConfig.YSQlLogMinErrorStatement;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class KubernetesConfigureExportTelemetryConfigTest extends KubernetesUpgradeTaskTest {

  private KubernetesConfigureExportTelemetryConfig task;

  @Before
  public void setUp() {
    super.setUp();
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    this.task =
        new KubernetesConfigureExportTelemetryConfig(
            mockBaseTaskDependencies, mockOperatorStatusUpdaterFactory);
  }

  private TaskInfo submitTask(ExportTelemetryConfigParams params) {
    return submitTask(params, TaskType.KubernetesConfigureExportTelemetryConfig, commissioner);
  }

  private static AuditLogConfig buildAuditLogConfig() {
    AuditLogConfig auditLogConfig = new AuditLogConfig();
    YSQLAuditConfig ysql = new YSQLAuditConfig();
    ysql.setEnabled(true);
    auditLogConfig.setYsqlAuditConfig(ysql);
    return auditLogConfig;
  }

  private static QueryLogConfig buildQueryLogConfig() {
    QueryLogConfig queryLogConfig = new QueryLogConfig();
    YSQLQueryLogConfig ysql = new YSQLQueryLogConfig();
    ysql.setEnabled(true);
    ysql.setLogStatement(YSQLLogStatement.ALL);
    ysql.setLogMinErrorStatement(YSQlLogMinErrorStatement.ERROR);
    ysql.setLogErrorVerbosity(YSQLLogErrorVerbosity.DEFAULT);
    queryLogConfig.setYsqlQueryLogConfig(ysql);
    return queryLogConfig;
  }

  @Test
  public void testRunOnSingleAZAppliesBothConfigsAndPersists() {
    setupUniverseSingleAZ(false, false);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.skipOpentelemetryOperatorCheck.getKey(), "true");
    task.setUserTaskUUID(UUID.randomUUID());

    AuditLogConfig auditLogConfig = buildAuditLogConfig();
    QueryLogConfig queryLogConfig = buildQueryLogConfig();

    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    params.setTelemetryConfig(TelemetryConfig.of(auditLogConfig, queryLogConfig, null));
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Helm upgrade is invoked at least once (one POD_INFO + multiple HELM_UPGRADE per AZ).
    verify(mockKubernetesManager, atLeastOnce())
        .helmUpgrade(any(UUID.class), any(), any(), any(), any(), any());

    // The persist subtask must be in the resulting subtask DAG.
    boolean persistRan =
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.UpdateAndPersistExportTelemetryConfig);
    assertTrue(
        "UpdateAndPersistExportTelemetryConfig must run as a subtask, found types: "
            + taskInfo.getSubTasks().stream().map(t -> t.getTaskType().name()).distinct().toList(),
        persistRan);
  }

  @Test
  public void testRunSchedulesPrecheckWhenSkipFlagOff() {
    setupUniverseSingleAZ(false, false);
    // Default: skipOpentelemetryOperatorCheck is false -> precheck must be scheduled.
    task.setUserTaskUUID(UUID.randomUUID());

    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    params.setTelemetryConfig(TelemetryConfig.of(buildAuditLogConfig(), null, null));
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;

    when(mockKubernetesManager.getPodObject(any(), any(), any())).thenReturn(null);

    TaskInfo taskInfo = submitTask(params);
    // We don't care whether the task succeeds end-to-end here -- just that the precheck was
    // scheduled. Some prechecks may fail in the in-memory fixture, which is fine; the assertion
    // is purely structural.
    boolean precheckScheduled =
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckOpentelemetryOperator);
    assertTrue("CheckOpentelemetryOperator precheck must be scheduled", precheckScheduled);
  }

  @Test
  public void testRunSchedulesKubernetesHelmUpgradeSubtask() {
    setupUniverseSingleAZ(false, false);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.skipOpentelemetryOperatorCheck.getKey(), "true");
    task.setUserTaskUUID(UUID.randomUUID());

    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    params.setTelemetryConfig(TelemetryConfig.of(buildAuditLogConfig(), null, null));
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // The K8s upgrade path always schedules at least one HELM_UPGRADE subtask via
    // KubernetesCommandExecutor. Exact count varies with AZ count and rolling vs non-rolling, so
    // assert structurally rather than on an exact number.
    boolean helmUpgradeScheduled =
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.KubernetesCommandExecutor);
    assertTrue("KubernetesCommandExecutor subtask must be scheduled", helmUpgradeScheduled);
  }
}
