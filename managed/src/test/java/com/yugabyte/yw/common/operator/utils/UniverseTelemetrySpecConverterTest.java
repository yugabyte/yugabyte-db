// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.audit.otel.OtelCollectorUtil;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.common.operator.utils.UniverseTelemetrySpecConverter.ExporterUuidResolver;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.exporters.server.ServerLogLevel;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import io.yugabyte.operator.v1alpha1.ybuniversespec.Telemetry;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.AuditLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.ControllerLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.MasterLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.Metrics;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.QueryLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.TserverLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.YsqlConnMgrLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.auditlogs.YcqlAuditConfig;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.auditlogs.YsqlAuditConfig;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.querylogs.YsqlQueryLogConfig;
import java.util.Arrays;
import java.util.Collections;
import java.util.UUID;
import org.junit.Test;

/**
 * Conversion of a universe CR's telemetry block, and the property the whole design hinges on: the
 * config it produces must compare equal to the config that gets persisted from it, or {@code
 * OperatorUtils.shouldUpdateTelemetry} never converges and every reconcile pass rolling-restarts
 * the universe. {@link #testConvergesAfterApply} is that check; the rest cover the derived fields
 * individually.
 *
 * <p>Pure unit test - no DB or Kubernetes client, the exporter UUID resolution is stubbed.
 */
public class UniverseTelemetrySpecConverterTest {

  private static final UUID EXPORTER_UUID = UUID.fromString("11111111-2222-3333-4444-555555555555");
  private static final ObjectMapper MAPPER = new ObjectMapper();

  private static final ExporterUuidResolver RESOLVER =
      name -> {
        if (!"datadog-prod".equals(name)) {
          throw new Exception("unexpected provider name " + name);
        }
        return EXPORTER_UUID;
      };

  private static io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.auditlogs.Exporters
      auditExporter() {
    io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.auditlogs.Exporters e =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.auditlogs.Exporters();
    e.setTelemetryProvider("datadog-prod");
    e.setAdditionalTags(Collections.singletonMap("env", "prod"));
    return e;
  }

  private static io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.querylogs.Exporters
      queryExporter() {
    io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.querylogs.Exporters e =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.querylogs.Exporters();
    e.setTelemetryProvider("datadog-prod");
    e.setSendBatchSize(250L);
    return e;
  }

  private static io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.metrics.Exporters
      metricsExporter() {
    io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.metrics.Exporters e =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.metrics.Exporters();
    e.setTelemetryProvider("datadog-prod");
    e.setMetricsPrefix("ybdb.");
    return e;
  }

  private static io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.masterlogs.Exporters
      masterExporter() {
    io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.masterlogs.Exporters e =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.masterlogs.Exporters();
    e.setTelemetryProvider("datadog-prod");
    return e;
  }

  private static io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.tserverlogs.Exporters
      tserverExporter() {
    io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.tserverlogs.Exporters e =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.tserverlogs.Exporters();
    e.setTelemetryProvider("datadog-prod");
    return e;
  }

  private static io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.ysqlconnmgrlogs.Exporters
      connMgrExporter() {
    io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.ysqlconnmgrlogs.Exporters e =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.ysqlconnmgrlogs.Exporters();
    e.setTelemetryProvider("datadog-prod");
    return e;
  }

  private static io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.controllerlogs.Exporters
      controllerExporter() {
    io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.controllerlogs.Exporters e =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.controllerlogs.Exporters();
    e.setTelemetryProvider("datadog-prod");
    return e;
  }

  private static Telemetry fullSpec() {
    Telemetry telemetry = new Telemetry();

    AuditLogs auditLogs = new AuditLogs();
    YsqlAuditConfig ysql = new YsqlAuditConfig();
    ysql.setClasses(Arrays.asList(YsqlAuditConfig.Classes.WRITE, YsqlAuditConfig.Classes.DDL));
    ysql.setLogParameter(true);
    ysql.setLogLevel(YsqlAuditConfig.LogLevel.LOG);
    ysql.setLogRetentionDays(7L);
    auditLogs.setYsqlAuditConfig(ysql);
    YcqlAuditConfig ycql = new YcqlAuditConfig();
    ycql.setIncludedCategories(Arrays.asList(YcqlAuditConfig.IncludedCategories.DDL));
    ycql.setLogLevel(YcqlAuditConfig.LogLevel.ERROR);
    auditLogs.setYcqlAuditConfig(ycql);
    auditLogs.setExporters(Collections.singletonList(auditExporter()));
    telemetry.setAuditLogs(auditLogs);

    QueryLogs queryLogs = new QueryLogs();
    YsqlQueryLogConfig queryLogConfig = new YsqlQueryLogConfig();
    queryLogConfig.setLogStatement(YsqlQueryLogConfig.LogStatement.ALL);
    queryLogConfig.setLogMinDurationStatement(500L);
    queryLogConfig.setLogLinePrefix("%m [%p] ");
    queryLogs.setYsqlQueryLogConfig(queryLogConfig);
    queryLogs.setExporters(Collections.singletonList(queryExporter()));
    telemetry.setQueryLogs(queryLogs);

    Metrics metrics = new Metrics();
    metrics.setExporters(Collections.singletonList(metricsExporter()));
    telemetry.setMetrics(metrics);

    MasterLogs masterLogs = new MasterLogs();
    masterLogs.setMinLevel(MasterLogs.MinLevel.WARNING);
    masterLogs.setExporters(Collections.singletonList(masterExporter()));
    telemetry.setMasterLogs(masterLogs);

    TserverLogs tserverLogs = new TserverLogs();
    tserverLogs.setExporters(Collections.singletonList(tserverExporter()));
    telemetry.setTserverLogs(tserverLogs);

    YsqlConnMgrLogs connMgrLogs = new YsqlConnMgrLogs();
    connMgrLogs.setExporters(Collections.singletonList(connMgrExporter()));
    telemetry.setYsqlConnMgrLogs(connMgrLogs);

    ControllerLogs controllerLogs = new ControllerLogs();
    controllerLogs.setExporters(Collections.singletonList(controllerExporter()));
    telemetry.setControllerLogs(controllerLogs);

    return telemetry;
  }

  /** What the task writes to export_telemetry_config, read back through the DbJson round trip. */
  private static TelemetryConfig persist(TelemetryConfig desired) throws Exception {
    TelemetryConfig stored =
        MAPPER.readValue(MAPPER.writeValueAsString(desired), TelemetryConfig.class);
    if (stored.getAuditLogConfig() != null) {
      stored.getAuditLogConfig().normalizeExportActive();
    }
    if (stored.getQueryLogConfig() != null) {
      stored.getQueryLogConfig().normalizeExportActive();
    }
    return MAPPER.readValue(MAPPER.writeValueAsString(stored), TelemetryConfig.class);
  }

  @Test
  public void testFullSpecConversion() throws Exception {
    TelemetryConfig desired =
        UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER);

    assertTrue(desired.getAuditLogConfig().getYsqlAuditConfig().isEnabled());
    assertTrue(desired.getAuditLogConfig().getYsqlAuditConfig().isLogParameter());
    assertEquals(
        Integer.valueOf(7), desired.getAuditLogConfig().getYsqlAuditConfig().getLogRetentionDays());
    assertEquals(2, desired.getAuditLogConfig().getYsqlAuditConfig().getClasses().size());
    assertTrue(desired.getAuditLogConfig().getYcqlAuditConfig().isEnabled());
    assertTrue(desired.getAuditLogConfig().isExportActive());
    assertEquals(
        EXPORTER_UUID,
        desired.getAuditLogConfig().getUniverseLogsExporterConfig().get(0).getExporterUuid());
    assertEquals(
        Collections.singletonMap("env", "prod"),
        desired.getAuditLogConfig().getUniverseLogsExporterConfig().get(0).getAdditionalTags());

    assertTrue(desired.getQueryLogConfig().getYsqlQueryLogConfig().isEnabled());
    assertTrue(desired.getQueryLogConfig().isExportActive());
    assertEquals(
        "%m [%p] ", desired.getQueryLogConfig().getYsqlQueryLogConfig().getLogLinePrefix());
    assertEquals(
        250, desired.getQueryLogConfig().getUniverseLogsExporterConfig().get(0).getSendBatchSize());

    assertEquals(
        OtelCollectorUtil.K8S_SUPPORTED_SCRAPE_TARGETS,
        desired.getMetricsExportConfig().getScrapeConfigTargets());
    assertEquals(
        MetricCollectionLevel.NORMAL, desired.getMetricsExportConfig().getCollectionLevel());
    assertEquals(Integer.valueOf(30), desired.getMetricsExportConfig().getScrapeIntervalSeconds());
    assertEquals(
        "ybdb.",
        desired
            .getMetricsExportConfig()
            .getUniverseMetricsExporterConfig()
            .get(0)
            .getMetricsPrefix());

    assertEquals(ServerLogLevel.WARNING, desired.getMasterLogConfig().getMinLevel());
    assertEquals(Double.valueOf(0.99), desired.getMasterLogConfig().getNoiseSampleDropRatio());
    assertEquals(ServerLogLevel.WARNING, desired.getTserverLogConfig().getMinLevel());
    assertEquals(1, desired.getYsqlConnMgrLogConfig().getUniverseLogsExporterConfig().size());
    assertEquals(1, desired.getControllerLogConfig().getUniverseLogsExporterConfig().size());
    assertEquals(null, desired.getNodeAgentLogConfig());
    assertEquals(null, desired.getYnpLogConfig());
  }

  @Test
  public void testConvergesAfterApply() throws Exception {
    TelemetryConfig applied =
        UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER);
    TelemetryConfig stored = persist(applied);
    // Reconciling the identical CR against the stored config must report no change for any type.
    TelemetryConfig desired =
        UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER);
    for (ExportType type : ExportType.values()) {
      assertFalse(
          "section " + type + " reported a difference after apply",
          OperatorUtils.telemetrySectionDiffers(type, desired, stored));
    }
  }

  @Test
  public void testEmptySpecConvergesAgainstEmptyCurrent() throws Exception {
    TelemetryConfig desired =
        UniverseTelemetrySpecConverter.toTelemetryConfig(new Telemetry(), RESOLVER);
    TelemetryConfig current = new TelemetryConfig();
    for (ExportType type : ExportType.values()) {
      assertFalse(type.name(), OperatorUtils.telemetrySectionDiffers(type, desired, current));
    }
  }

  @Test
  public void testAddingASectionTriggersOnlyThatSection() throws Exception {
    Telemetry telemetry = new Telemetry();
    AuditLogs auditLogs = new AuditLogs();
    auditLogs.setYsqlAuditConfig(new YsqlAuditConfig());
    auditLogs.setExporters(Collections.singletonList(auditExporter()));
    telemetry.setAuditLogs(auditLogs);

    TelemetryConfig desired = UniverseTelemetrySpecConverter.toTelemetryConfig(telemetry, RESOLVER);
    TelemetryConfig current = new TelemetryConfig();
    for (ExportType type : ExportType.values()) {
      assertEquals(
          type.name(),
          type == ExportType.AUDIT_LOGS,
          OperatorUtils.telemetrySectionDiffers(type, desired, current));
    }
    // And it converges once applied.
    TelemetryConfig stored = persist(desired);
    for (ExportType type : ExportType.values()) {
      assertFalse(type.name(), OperatorUtils.telemetrySectionDiffers(type, desired, stored));
    }
  }

  @Test
  public void testChangedFieldTriggers() throws Exception {
    TelemetryConfig stored =
        persist(UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER));
    Telemetry changed = fullSpec();
    changed.getTserverLogs().setMinLevel(TserverLogs.MinLevel.ERROR);
    TelemetryConfig desired = UniverseTelemetrySpecConverter.toTelemetryConfig(changed, RESOLVER);
    assertTrue(OperatorUtils.telemetrySectionDiffers(ExportType.TSERVER_LOGS, desired, stored));
    assertFalse(OperatorUtils.telemetrySectionDiffers(ExportType.AUDIT_LOGS, desired, stored));
  }

  @Test
  public void testCurrentWithDisabledYsqlAuditIsTreatedAsAbsent() throws Exception {
    // Audit config present in the universe but disabled (a shape the v1 API can produce) reads as
    // "not configured", so a CR that omits it does not re-fire, and a CR that has it does.
    TelemetryConfig stored =
        persist(UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER));
    stored.getAuditLogConfig().getYsqlAuditConfig().setEnabled(false);
    stored.getAuditLogConfig().getYcqlAuditConfig().setEnabled(false);
    TelemetryConfig desired =
        UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER);
    assertTrue(OperatorUtils.telemetrySectionDiffers(ExportType.AUDIT_LOGS, desired, stored));
  }

  @Test
  public void testDerivedFieldsAreNotComparedRaw() throws Exception {
    // A stored config whose derived fields were written by another path must not read as a change.
    TelemetryConfig stored =
        persist(UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER));
    stored.getAuditLogConfig().setExportActive(false);
    stored.getQueryLogConfig().setExportActive(false);
    TelemetryConfig desired =
        UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER);
    assertFalse(OperatorUtils.telemetrySectionDiffers(ExportType.AUDIT_LOGS, desired, stored));
    assertFalse(OperatorUtils.telemetrySectionDiffers(ExportType.QUERY_LOGS, desired, stored));
  }

  @Test
  public void testComparisonDoesNotMutateCurrent() throws Exception {
    TelemetryConfig stored =
        persist(UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER));
    TelemetryConfig desired =
        UniverseTelemetrySpecConverter.toTelemetryConfig(fullSpec(), RESOLVER);
    OperatorUtils.telemetrySectionDiffers(ExportType.AUDIT_LOGS, desired, stored);
    assertTrue(stored.getAuditLogConfig().getYsqlAuditConfig().isEnabled());
    assertTrue(stored.getAuditLogConfig().isExportActive());
    assertTrue(desired.getAuditLogConfig().getYsqlAuditConfig().isEnabled());
  }
}
