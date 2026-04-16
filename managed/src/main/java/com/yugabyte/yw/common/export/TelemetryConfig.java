// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;

@Getter
@Setter
@NoArgsConstructor
@AllArgsConstructor
@ToString
public class TelemetryConfig {

  private AuditLogConfig auditLogConfig = null;

  private QueryLogConfig queryLogConfig = null;

  private MetricsExportConfig metricsExportConfig = null;

  /** Build from the three V1 configs (e.g. from task params). */
  public static TelemetryConfig of(
      AuditLogConfig auditLogConfig,
      QueryLogConfig queryLogConfig,
      MetricsExportConfig metricsExportConfig) {
    return new TelemetryConfig(auditLogConfig, queryLogConfig, metricsExportConfig);
  }
}
