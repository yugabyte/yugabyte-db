// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;
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
      default:
        throw new IllegalArgumentException("Unhandled export type: " + type);
    }
  }

  /** True if any export section is set (non-null). */
  public boolean hasAnyConfig() {
    return Arrays.stream(ExportType.values()).anyMatch(t -> section(t) != null);
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

  /** Build from all telemetry configs (e.g. from task params). */
  public static TelemetryConfig of(
      AuditLogConfig auditLogConfig,
      QueryLogConfig queryLogConfig,
      MetricsExportConfig metricsExportConfig,
      MasterLogConfig masterLogConfig) {
    return new TelemetryConfig(
        auditLogConfig, queryLogConfig, metricsExportConfig, masterLogConfig);
  }
}
