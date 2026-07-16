// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.audit.otel.OtelCollectorUtil;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

/** Task params for the unified ConfigureExportTelemetryConfig task. */
@Data
@EqualsAndHashCode(callSuper = false)
@Slf4j
@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ExportTelemetryConfigParams.Converter.class)
public class ExportTelemetryConfigParams extends UpgradeTaskParams {

  private TelemetryConfig telemetryConfig;

  private List<ExportType> modifiedExportTypes = new ArrayList<>();

  /** Delay in seconds between master server restarts (rolling upgrade). Default 0. */
  public Integer delayBetweenMasterServers = 0;

  /** Delay in seconds between tserver restarts (rolling upgrade). Default 0. */
  public Integer delayBetweenTserverServers = 0;

  /**
   * True when the caller explicitly chose an upgrade option (e.g. set rollingUpgrade in the API
   * request). When false, the handler may downgrade a collector-only change to NON_RESTART_UPGRADE.
   */
  private boolean upgradeOptionExplicitlySet = false;

  public AuditLogConfig getAuditLogConfig() {
    return telemetryConfig != null ? telemetryConfig.getAuditLogConfig() : null;
  }

  public QueryLogConfig getQueryLogConfig() {
    return telemetryConfig != null ? telemetryConfig.getQueryLogConfig() : null;
  }

  public MetricsExportConfig getMetricsExportConfig() {
    return telemetryConfig != null ? telemetryConfig.getMetricsExportConfig() : null;
  }

  public MasterLogConfig getMasterLogConfig() {
    return telemetryConfig != null ? telemetryConfig.getMasterLogConfig() : null;
  }

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);

    AuditLogConfig auditLogConfig = getAuditLogConfig();
    if (modifiedExportTypes.contains(ExportType.AUDIT_LOGS)
        && auditLogConfig != null
        && auditLogConfig.isExportActive()
        && CollectionUtils.isEmpty(auditLogConfig.getUniverseLogsExporterConfig())) {
      throw new PlatformServiceException(
          play.mvc.Http.Status.BAD_REQUEST,
          String.format(
              "Audit log config is set to export active, but no exporter configured on universe"
                  + " '%s'.",
              universe.getUniverseUUID()));
    }
    QueryLogConfig queryLogConfig = getQueryLogConfig();
    if (modifiedExportTypes.contains(ExportType.QUERY_LOGS)
        && queryLogConfig != null
        && queryLogConfig.isExportActive()
        && CollectionUtils.isEmpty(queryLogConfig.getUniverseLogsExporterConfig())) {
      throw new PlatformServiceException(
          play.mvc.Http.Status.BAD_REQUEST,
          String.format(
              "Query log config is set to export active, but no exporter configured on universe"
                  + " '%s'.",
              universe.getUniverseUUID()));
    }

    // Validate that every referenced telemetry exporter exists up front, so a missing/deleted
    // provider fails synchronously with a 400 here instead of deep inside the task at config
    // render time. Only the active exporters are checked (the ones the task will resolve), so
    // disabling export with a since-deleted provider still works.
    Set<UUID> exporterUuids = new HashSet<>();
    if (OtelCollectorUtil.isAuditLogExportEnabledInUniverse(getAuditLogConfig())) {
      getAuditLogConfig()
          .getUniverseLogsExporterConfig()
          .forEach(c -> exporterUuids.add(c.getExporterUuid()));
    }
    if (OtelCollectorUtil.isQueryLogExportEnabledInUniverse(getQueryLogConfig())) {
      getQueryLogConfig()
          .getUniverseLogsExporterConfig()
          .forEach(c -> exporterUuids.add(c.getExporterUuid()));
    }
    if (OtelCollectorUtil.isMetricsExportEnabledInUniverse(getMetricsExportConfig())) {
      getMetricsExportConfig()
          .getUniverseMetricsExporterConfig()
          .forEach(c -> exporterUuids.add(c.getExporterUuid()));
    }
    if (OtelCollectorUtil.isMasterLogExportEnabledInUniverse(getMasterLogConfig())) {
      getMasterLogConfig()
          .getUniverseLogsExporterConfig()
          .forEach(c -> exporterUuids.add(c.getExporterUuid()));
    }
    if (!exporterUuids.isEmpty()) {
      TelemetryProviderService telemetryProviderService =
          StaticInjectorHolder.injector().instanceOf(TelemetryProviderService.class);
      exporterUuids.forEach(telemetryProviderService::getOrBadRequest);
    }

    if (!Util.isKubernetesBasedUniverse(universe)) {
      return;
    }

    if (OtelCollectorUtil.isMetricsExportEnabledInUniverse(getMetricsExportConfig())) {
      throw new PlatformServiceException(
          play.mvc.Http.Status.BAD_REQUEST,
          "Metrics export is not yet supported for kubernetes based universes.");
    }

    if (OtelCollectorUtil.isMasterLogExportEnabledInUniverse(getMasterLogConfig())) {
      throw new PlatformServiceException(
          play.mvc.Http.Status.BAD_REQUEST,
          "Master log export is not yet supported for kubernetes based universes.");
    }

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    boolean wantsLogExport =
        OtelCollectorUtil.isAuditLogExportEnabledInUniverse(getAuditLogConfig())
            || OtelCollectorUtil.isQueryLogExportEnabledInUniverse(getQueryLogConfig());
    if (wantsLogExport) {
      if (!KubernetesUtil.isExporterSupported(userIntent.ybSoftwareVersion)) {
        throw new PlatformServiceException(
            play.mvc.Http.Status.BAD_REQUEST,
            String.format(
                "Log exporter is not supported for universe '%s' running version '%s'. Please"
                    + " upgrade to version '%s' or '%s', or disable the exporter.",
                universe.getUniverseUUID(),
                userIntent.ybSoftwareVersion,
                KubernetesUtil.MIN_VERSION_OTEL_SUPPORT_STABLE,
                KubernetesUtil.MIN_VERSION_OTEL_SUPPORT_PREVIEW));
      }
    }

    // Query log export on K8s needs the newer chart that renders a query receiver via the
    // spec.config passthrough. Older charts are audit-only, so reject query log export below the
    // passthrough version instead of silently dropping (or mis-wiring) it in the structured
    // fallback.
    if (OtelCollectorUtil.isQueryLogExportEnabledInUniverse(getQueryLogConfig())
        && !OtelCollectorUtil.supportsOtelConfigPassthrough(userIntent.ybSoftwareVersion)) {
      throw new PlatformServiceException(
          play.mvc.Http.Status.BAD_REQUEST,
          String.format(
              "Query log export is not supported for kubernetes universe '%s' running version"
                  + " '%s'. Please upgrade to version '%s' or '%s', or disable query log export.",
              universe.getUniverseUUID(),
              userIntent.ybSoftwareVersion,
              OtelCollectorUtil.OTEL_HELM_CONFIG_PASSTHROUGH_STABLE_VERSION,
              OtelCollectorUtil.OTEL_HELM_CONFIG_PASSTHROUGH_PREVIEW_VERSION));
    }
  }

  public static class Converter extends BaseConverter<ExportTelemetryConfigParams> {}
}
