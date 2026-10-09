// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import com.yugabyte.yw.models.helpers.exporters.UniverseExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.ControllerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.NodeAgentLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.TServerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.YnpLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.YsqlConnMgrLogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;

/**
 * Aggregate of all telemetry export sections. Prefer the {@link #builder()} to set only the
 * section(s) you care about (e.g. {@code TelemetryConfig.builder().auditLogConfig(c).build()}) so
 * call sites do not enumerate null placeholders for the other types and stay unchanged when a new
 * export type is added.
 */
@Getter
@Setter
@NoArgsConstructor
@AllArgsConstructor
@Builder
@ToString
public class TelemetryConfig {

  private AuditLogConfig auditLogConfig = null;

  private QueryLogConfig queryLogConfig = null;

  private MetricsExportConfig metricsExportConfig = null;

  private MasterLogConfig masterLogConfig = null;

  private TServerLogConfig tserverLogConfig = null;

  private YsqlConnMgrLogConfig ysqlConnMgrLogConfig = null;

  private NodeAgentLogConfig nodeAgentLogConfig = null;

  private YnpLogConfig ynpLogConfig = null;

  private ControllerLogConfig controllerLogConfig = null;

  /**
   * The config section for a given export type, or null when that type is disabled. This is the one
   * place that maps an {@link ExportType} to its backing field: {@link #diff} and {@link
   * #hasAnyConfig} derive from it, so adding a new export type means adding its field plus one case
   * here (the {@code default} throws to make a forgotten case fail loudly).
   */
  public Object section(ExportType type) {
    switch (type) {
      case AUDIT_LOGS:
        return auditLogConfig;
      case QUERY_LOGS:
        return queryLogConfig;
      case METRICS:
        return metricsExportConfig;
      case MASTER_LOGS:
        return masterLogConfig;
      case TSERVER_LOGS:
        return tserverLogConfig;
      case YSQL_CONN_MGR_LOGS:
        return ysqlConnMgrLogConfig;
      case NODE_AGENT_LOGS:
        return nodeAgentLogConfig;
      case YNP_LOGS:
        return ynpLogConfig;
      case CONTROLLER_LOGS:
        return controllerLogConfig;
      default:
        throw new IllegalArgumentException("Unhandled export type: " + type);
    }
  }

  /**
   * Every exporter UUID referenced by any section. Second mapping of {@link ExportType} to a field,
   * deliberately kept beside {@link #section} and fail-loud for the same reason: a forgotten export
   * type here would report a telemetry provider as unreferenced, letting it be deleted while a
   * universe still exports to it. That leaves a dangling exporterUuid, and every later task that
   * regenerates the collector config for that universe - ResumeUniverse included - fails in
   * appendLogExporter with "Invalid Telemetry Provider UUID".
   */
  public Set<UUID> referencedExporterUuids() {
    Set<UUID> referenced = new HashSet<>();
    for (ExportType type : ExportType.values()) {
      for (UniverseExporterConfig exporter : exporters(type)) {
        if (exporter != null && exporter.getExporterUuid() != null) {
          referenced.add(exporter.getExporterUuid());
        }
      }
    }
    return referenced;
  }

  private List<? extends UniverseExporterConfig> exporters(ExportType type) {
    switch (type) {
      case AUDIT_LOGS:
        return auditLogConfig == null
            ? List.of()
            : nullToEmpty(auditLogConfig.getUniverseLogsExporterConfig());
      case QUERY_LOGS:
        return queryLogConfig == null
            ? List.of()
            : nullToEmpty(queryLogConfig.getUniverseLogsExporterConfig());
      case METRICS:
        return metricsExportConfig == null
            ? List.of()
            : nullToEmpty(metricsExportConfig.getUniverseMetricsExporterConfig());
      case MASTER_LOGS:
        return masterLogConfig == null
            ? List.of()
            : nullToEmpty(masterLogConfig.getUniverseLogsExporterConfig());
      case TSERVER_LOGS:
        return tserverLogConfig == null
            ? List.of()
            : nullToEmpty(tserverLogConfig.getUniverseLogsExporterConfig());
      case YSQL_CONN_MGR_LOGS:
        return ysqlConnMgrLogConfig == null
            ? List.of()
            : nullToEmpty(ysqlConnMgrLogConfig.getUniverseLogsExporterConfig());
      case NODE_AGENT_LOGS:
        return nodeAgentLogConfig == null
            ? List.of()
            : nullToEmpty(nodeAgentLogConfig.getUniverseLogsExporterConfig());
      case YNP_LOGS:
        return ynpLogConfig == null
            ? List.of()
            : nullToEmpty(ynpLogConfig.getUniverseLogsExporterConfig());
      case CONTROLLER_LOGS:
        return controllerLogConfig == null
            ? List.of()
            : nullToEmpty(controllerLogConfig.getUniverseLogsExporterConfig());
      default:
        throw new IllegalArgumentException("Unhandled export type: " + type);
    }
  }

  private static <T> List<T> nullToEmpty(List<T> list) {
    return list == null ? List.of() : list;
  }

  /** True if any export section is set (non-null). */
  public boolean hasAnyConfig() {
    return Arrays.stream(ExportType.values()).anyMatch(t -> section(t) != null);
  }

  /**
   * True when this config contributes PostgreSQL settings to the ysql_pg_conf_csv gflag: audit
   * logging (pgaudit.*) and query logging (log_*) do, metrics and master-log export do not.
   */
  public boolean requiresYsqlPgConfCsv() {
    return auditLogConfig != null || queryLogConfig != null;
  }

  /**
   * Export types whose section differs between {@code desired} and {@code current} (either may be
   * null). Derives from {@link #section}, so adding an export type needs no change here.
   */
  public static List<ExportType> diff(TelemetryConfig desired, TelemetryConfig current) {
    TelemetryConfig a = desired != null ? desired : new TelemetryConfig();
    TelemetryConfig b = current != null ? current : new TelemetryConfig();
    List<ExportType> modified = new ArrayList<>();
    for (ExportType type : ExportType.values()) {
      if (!Objects.equals(a.section(type), b.section(type))) {
        modified.add(type);
      }
    }
    return modified;
  }

  /** Build from audit/query/metrics configs (e.g. from task params). Master logs default off. */
  public static TelemetryConfig of(
      AuditLogConfig auditLogConfig,
      QueryLogConfig queryLogConfig,
      MetricsExportConfig metricsExportConfig) {
    return of(auditLogConfig, queryLogConfig, metricsExportConfig, null);
  }

  /** Build from audit/query/metrics/master configs. TServer logs default off. */
  public static TelemetryConfig of(
      AuditLogConfig auditLogConfig,
      QueryLogConfig queryLogConfig,
      MetricsExportConfig metricsExportConfig,
      MasterLogConfig masterLogConfig) {
    return of(auditLogConfig, queryLogConfig, metricsExportConfig, masterLogConfig, null);
  }

  /** Build from all telemetry configs (e.g. from task params). */
  public static TelemetryConfig of(
      AuditLogConfig auditLogConfig,
      QueryLogConfig queryLogConfig,
      MetricsExportConfig metricsExportConfig,
      MasterLogConfig masterLogConfig,
      TServerLogConfig tserverLogConfig) {
    return TelemetryConfig.builder()
        .auditLogConfig(auditLogConfig)
        .queryLogConfig(queryLogConfig)
        .metricsExportConfig(metricsExportConfig)
        .masterLogConfig(masterLogConfig)
        .tserverLogConfig(tserverLogConfig)
        .build();
  }
}
