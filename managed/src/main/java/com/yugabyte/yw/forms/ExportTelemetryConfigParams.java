// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

/**
 * Task params for the unified ConfigureExportTelemetryConfig task. Holds telemetry configs (audit,
 * query, metrics) in a single {@link TelemetryConfig} so the task can reuse existing subtasks and
 * OTEL logic. Delegate getters allow call sites to use getAuditLogConfig() etc.
 */
@Data
@EqualsAndHashCode(callSuper = false)
@Slf4j
@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ExportTelemetryConfigParams.Converter.class)
public class ExportTelemetryConfigParams extends UpgradeTaskParams {

  private TelemetryConfig telemetryConfig;

  /** Delay in seconds between master server restarts (rolling upgrade). Default 0. */
  public Integer delayBetweenMasterServers = 0;

  /** Delay in seconds between tserver restarts (rolling upgrade). Default 0. */
  public Integer delayBetweenTserverServers = 0;

  public AuditLogConfig getAuditLogConfig() {
    return telemetryConfig != null ? telemetryConfig.getAuditLogConfig() : null;
  }

  public QueryLogConfig getQueryLogConfig() {
    return telemetryConfig != null ? telemetryConfig.getQueryLogConfig() : null;
  }

  public MetricsExportConfig getMetricsExportConfig() {
    return telemetryConfig != null ? telemetryConfig.getMetricsExportConfig() : null;
  }

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return false;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    MetricsExportConfig metricsExportConfig = getMetricsExportConfig();
    if (metricsExportConfig != null
        && metricsExportConfig.isExportActive()
        && universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.kubernetes)) {
      throw new com.yugabyte.yw.common.PlatformServiceException(
          play.mvc.Http.Status.BAD_REQUEST,
          "Metrics export is not yet supported for kubernetes based universes.");
    }
  }

  public static class Converter extends BaseConverter<ExportTelemetryConfigParams> {}
}
