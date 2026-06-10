// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.models.AuditLogsTelemetrySpec;
import api.v2.models.MetricsTelemetrySpec;
import api.v2.models.QueryLogsTelemetrySpec;
import api.v2.models.UniverseLogsExporterConfig;
import api.v2.models.UniverseMetricsExporterConfig;
import api.v2.models.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.Test;

public class ExportTelemetryConfigMapperTest {

  // --- Reverse mapper (internal -> v2) ---

  @Test
  public void reverseAuditMapsUuidAndTags() {
    UUID auditUuid = UUID.randomUUID();
    com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig auditExporter =
        new com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig();
    auditExporter.setExporterUuid(auditUuid);
    auditExporter.setAdditionalTags(Collections.singletonMap("team", "platform"));
    AuditLogConfig auditConfig = new AuditLogConfig();
    auditConfig.setUniverseLogsExporterConfig(List.of(auditExporter));

    api.v2.models.TelemetryConfig result =
        ExportTelemetryConfigMapper.toGenerated(auditConfig, null, null);
    AuditLogsTelemetrySpec audit = result.getAuditLogs();
    assertNotNull(audit);
    assertEquals(1, audit.getExporters().size());
    UniverseLogsExporterConfig entry = audit.getExporters().get(0);
    assertEquals(auditUuid, entry.getExporterUuid());
    assertEquals(Collections.singletonMap("team", "platform"), entry.getAdditionalTags());
  }

  @Test
  public void reverseQueryMapsBatchTuning() {
    com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig queryExporter =
        new com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig();
    queryExporter.setExporterUuid(UUID.randomUUID());
    queryExporter.setSendBatchMaxSize(2000);
    queryExporter.setSendBatchSize(250);
    queryExporter.setMemoryLimitMib(4096);
    QueryLogConfig queryConfig = new QueryLogConfig();
    queryConfig.setUniverseLogsExporterConfig(List.of(queryExporter));

    api.v2.models.TelemetryConfig result =
        ExportTelemetryConfigMapper.toGenerated(null, queryConfig, null);
    QueryLogsTelemetrySpec query = result.getQueryLogs();
    assertNotNull(query);
    UniverseQueryLogsExporterConfig entry = query.getExporters().get(0);
    assertEquals(Integer.valueOf(2000), entry.getSendBatchMaxSize());
    assertEquals(Integer.valueOf(250), entry.getSendBatchSize());
    assertEquals(Integer.valueOf(4096), entry.getMemoryLimitMib());
  }

  @Test
  public void reverseMetricsMapsBatchAndPrefix() {
    com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig metricsExporter =
        new com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig();
    metricsExporter.setExporterUuid(UUID.randomUUID());
    metricsExporter.setSendBatchMaxSize(500);
    metricsExporter.setMetricsPrefix("ybdb.");
    MetricsExportConfig metricsConfig = new MetricsExportConfig();
    metricsConfig.setUniverseMetricsExporterConfig(List.of(metricsExporter));

    api.v2.models.TelemetryConfig result =
        ExportTelemetryConfigMapper.toGenerated(null, null, metricsConfig);
    MetricsTelemetrySpec metrics = result.getMetrics();
    assertNotNull(metrics);
    UniverseMetricsExporterConfig entry = metrics.getExporters().get(0);
    assertEquals(Integer.valueOf(500), entry.getSendBatchMaxSize());
    assertEquals("ybdb.", entry.getMetricsPrefix());
  }

  @Test
  public void reverseAllNullsReturnsEmptyTelemetryConfig() {
    api.v2.models.TelemetryConfig result =
        ExportTelemetryConfigMapper.toGenerated(null, null, null);
    assertNull(result.getAuditLogs());
    assertNull(result.getQueryLogs());
    assertNull(result.getMetrics());
  }

  // --- Forward mapper (v2 -> internal) ---

  @Test
  public void forwardAuditEntryMapsUuidAndTags() {
    UUID uuid = UUID.randomUUID();
    api.v2.models.TelemetryConfig req = new api.v2.models.TelemetryConfig();
    AuditLogsTelemetrySpec audit = new AuditLogsTelemetrySpec();
    UniverseLogsExporterConfig entry = new UniverseLogsExporterConfig();
    entry.setExporterUuid(uuid);
    entry.setAdditionalTags(Collections.singletonMap("k", "v"));
    audit.setExporters(List.of(entry));
    req.setAuditLogs(audit);

    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    ExportTelemetryConfigMapper.fillParams(req, params);
    assertNotNull(params.getAuditLogConfig());
    com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig out =
        params.getAuditLogConfig().getUniverseLogsExporterConfig().get(0);
    assertEquals(uuid, out.getExporterUuid());
    assertEquals(Collections.singletonMap("k", "v"), out.getAdditionalTags());
  }

  @Test
  public void forwardQueryEntryCarriesBatchTuning() {
    api.v2.models.TelemetryConfig req = new api.v2.models.TelemetryConfig();
    QueryLogsTelemetrySpec query = new QueryLogsTelemetrySpec();
    UniverseQueryLogsExporterConfig entry = new UniverseQueryLogsExporterConfig();
    entry.setExporterUuid(UUID.randomUUID());
    entry.setSendBatchMaxSize(2000);
    query.setExporters(List.of(entry));
    req.setQueryLogs(query);

    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    ExportTelemetryConfigMapper.fillParams(req, params);
    assertNotNull(params.getQueryLogConfig());
    assertEquals(
        2000,
        params.getQueryLogConfig().getUniverseLogsExporterConfig().get(0).getSendBatchMaxSize());
  }

  @Test
  public void forwardMetricsEntryCarriesPrefix() {
    api.v2.models.TelemetryConfig req = new api.v2.models.TelemetryConfig();
    MetricsTelemetrySpec metrics = new MetricsTelemetrySpec();
    UniverseMetricsExporterConfig entry = new UniverseMetricsExporterConfig();
    entry.setExporterUuid(UUID.randomUUID());
    entry.setMetricsPrefix("ybdb.");
    metrics.setExporters(List.of(entry));
    req.setMetrics(metrics);

    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    ExportTelemetryConfigMapper.fillParams(req, params);
    assertNotNull(params.getMetricsExportConfig());
    assertEquals(
        "ybdb.",
        params
            .getMetricsExportConfig()
            .getUniverseMetricsExporterConfig()
            .get(0)
            .getMetricsPrefix());
  }
}
