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
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.exporters.UniverseExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.SimpleServerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.TServerLogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
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

  public TServerLogConfig getTserverLogConfig() {
    return telemetryConfig != null ? telemetryConfig.getTserverLogConfig() : null;
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

    // Group the active exporters by signal. Two sets, not a uuid -> signal map, since the same
    // sink can be wired to both.
    Set<UUID> metricsSinks = new HashSet<>();
    Set<UUID> logSinks = new HashSet<>();
    if (OtelCollectorUtil.isMetricsExportEnabledInUniverse(getMetricsExportConfig())) {
      addExporterUuids(getMetricsExportConfig().getUniverseMetricsExporterConfig(), metricsSinks);
    }
    if (OtelCollectorUtil.isAuditLogExportEnabledInUniverse(auditLogConfig)) {
      addExporterUuids(auditLogConfig.getUniverseLogsExporterConfig(), logSinks);
    }
    if (OtelCollectorUtil.isQueryLogExportEnabledInUniverse(queryLogConfig)) {
      addExporterUuids(queryLogConfig.getUniverseLogsExporterConfig(), logSinks);
    }
    if (OtelCollectorUtil.isMasterLogExportEnabledInUniverse(getMasterLogConfig())) {
      addExporterUuids(getMasterLogConfig().getUniverseLogsExporterConfig(), logSinks);
    }
    if (OtelCollectorUtil.isTserverLogExportEnabledInUniverse(getTserverLogConfig())) {
      addExporterUuids(getTserverLogConfig().getUniverseLogsExporterConfig(), logSinks);
    }
    for (SimpleServerLogConfig serverLogConfig :
        OtelCollectorUtil.simpleServerLogConfigs(telemetryConfig)) {
      if (OtelCollectorUtil.isSimpleServerLogExportEnabledInUniverse(serverLogConfig)) {
        addExporterUuids(serverLogConfig.getUniverseLogsExporterConfig(), logSinks);
      }
    }

    // Resolve each referenced provider once, failing a missing/deleted one or an incompatible sink
    // with a 400 here instead of an unstartable collector config later. Only actively exporting
    // sections were collected, so disabling export with a since-deleted provider still works.
    Set<UUID> exporterUuids = new HashSet<>(logSinks);
    exporterUuids.addAll(metricsSinks);
    if (!exporterUuids.isEmpty()) {
      TelemetryProviderService telemetryProviderService =
          StaticInjectorHolder.injector().instanceOf(TelemetryProviderService.class);
      for (UUID exporterUuid : exporterUuids) {
        TelemetryProvider provider = telemetryProviderService.getOrBadRequest(exporterUuid);
        // Config and type are nullable columns, and @NotNull is only enforced on the save path.
        ProviderType providerType =
            provider.getConfig() != null ? provider.getConfig().getType() : null;
        if (providerType == null) {
          throw new PlatformServiceException(
              play.mvc.Http.Status.BAD_REQUEST,
              String.format(
                  "Exporter '%s' (%s) has no provider type configured and cannot be used for export"
                      + " on universe '%s'.",
                  provider.getName(), exporterUuid, universe.getUniverseUUID()));
        }
        if (metricsSinks.contains(exporterUuid) && !providerType.isAllowedForMetrics) {
          throw exporterNotAllowed(provider, providerType, "metrics", universe);
        }
        if (logSinks.contains(exporterUuid) && !providerType.isAllowedForLogs) {
          throw exporterNotAllowed(provider, providerType, "logs", universe);
        }
      }
    }

    if (!Util.isKubernetesBasedUniverse(universe)) {
      return;
    }

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    boolean anySimpleServerLogEnabled =
        OtelCollectorUtil.simpleServerLogConfigs(telemetryConfig).stream()
            .anyMatch(OtelCollectorUtil::isSimpleServerLogExportEnabledInUniverse);
    boolean wantsLogExport =
        OtelCollectorUtil.isAuditLogExportEnabledInUniverse(getAuditLogConfig())
            || OtelCollectorUtil.isQueryLogExportEnabledInUniverse(getQueryLogConfig())
            || OtelCollectorUtil.isMasterLogExportEnabledInUniverse(getMasterLogConfig())
            || OtelCollectorUtil.isTserverLogExportEnabledInUniverse(getTserverLogConfig())
            || anySimpleServerLogEnabled;
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

    // Query, metrics, master and tserver export on K8s need the newer chart that renders their
    // receivers via the spec.config passthrough. Older charts are audit-only, so reject them below
    // the passthrough version instead of silently dropping (or mis-wiring) them in the structured
    // fallback.
    boolean supportsPassthrough =
        OtelCollectorUtil.supportsOtelConfigPassthrough(userIntent.ybSoftwareVersion);
    boolean metricsEnabled =
        OtelCollectorUtil.isMetricsExportEnabledInUniverse(getMetricsExportConfig());
    requirePassthroughForK8s(
        supportsPassthrough,
        OtelCollectorUtil.isQueryLogExportEnabledInUniverse(getQueryLogConfig()),
        "Query log export",
        universe,
        userIntent);
    requirePassthroughForK8s(
        supportsPassthrough, metricsEnabled, "Metrics export", universe, userIntent);
    requirePassthroughForK8s(
        supportsPassthrough,
        OtelCollectorUtil.isMasterLogExportEnabledInUniverse(getMasterLogConfig()),
        "Master log export",
        universe,
        userIntent);
    requirePassthroughForK8s(
        supportsPassthrough,
        OtelCollectorUtil.isTserverLogExportEnabledInUniverse(getTserverLogConfig()),
        "TServer log export",
        universe,
        userIntent);
    // YSQL Connection Manager and YB-Controller logs are pod-local on K8s (they live in the
    // yb-tserver pod), so they are supported behind the same passthrough-chart gate as master/
    // tserver.
    requirePassthroughForK8s(
        supportsPassthrough,
        OtelCollectorUtil.isSimpleServerLogExportEnabledInUniverse(
            telemetryConfig != null ? telemetryConfig.getYsqlConnMgrLogConfig() : null),
        "YSQL Connection Manager log export",
        universe,
        userIntent);
    requirePassthroughForK8s(
        supportsPassthrough,
        OtelCollectorUtil.isSimpleServerLogExportEnabledInUniverse(
            telemetryConfig != null ? telemetryConfig.getControllerLogConfig() : null),
        "YB-Controller log export",
        universe,
        userIntent);

    // Block any requested export type whose source is not present in K8s pods (VM-only sources such
    // as node-agent and YNP). Driven by ExportType.isSupportedOnKubernetes(), so a new VM-only type
    // is rejected here automatically without editing this method.
    for (ExportType type : ExportType.values()) {
      if (!type.isSupportedOnKubernetes()
          && OtelCollectorUtil.isExportTypeActive(telemetryConfig, type)) {
        throw new PlatformServiceException(
            play.mvc.Http.Status.BAD_REQUEST,
            String.format(
                "%s export is not supported for kubernetes universe '%s'. It is available only on"
                    + " VM-based universes.",
                type, universe.getUniverseUUID()));
      }
    }

    // Metrics has extra K8s-specific target validation beyond the shared passthrough gate.
    if (metricsEnabled) {
      // Fail fast on an empty target list: downstream layers assume a validated, non-empty set
      // of K8s-servable targets (the config generator refuses to render otherwise).
      if (CollectionUtils.isEmpty(getMetricsExportConfig().getScrapeConfigTargets())) {
        throw new PlatformServiceException(
            play.mvc.Http.Status.BAD_REQUEST,
            String.format(
                "Scrape config targets must be specified for metrics export on kubernetes universe"
                    + " '%s'. Supported targets: %s.",
                universe.getUniverseUUID(), OtelCollectorUtil.K8S_SUPPORTED_SCRAPE_TARGETS));
      }

      // The collector sidecar can only scrape pod-local endpoints on K8s; there is no
      // node-exporter or node-agent in the DB pods, so reject those targets up front instead of
      // silently exporting nothing for them.
      Set<ScrapeConfigTargetType> unsupportedTargets =
          OtelCollectorUtil.getUnsupportedK8sScrapeTargets(getMetricsExportConfig());
      if (!unsupportedTargets.isEmpty()) {
        throw new PlatformServiceException(
            play.mvc.Http.Status.BAD_REQUEST,
            String.format(
                "Scrape config targets %s are not supported for kubernetes universe '%s'. Please"
                    + " retry with a subset of the supported targets: %s.",
                unsupportedTargets,
                universe.getUniverseUUID(),
                OtelCollectorUtil.K8S_SUPPORTED_SCRAPE_TARGETS));
      }
    }
  }

  private static void addExporterUuids(
      List<? extends UniverseExporterConfig> exporters, Set<UUID> uuids) {
    exporters.forEach(exporter -> uuids.add(exporter.getExporterUuid()));
  }

  private static PlatformServiceException exporterNotAllowed(
      TelemetryProvider provider, ProviderType providerType, String signal, Universe universe) {
    return new PlatformServiceException(
        play.mvc.Http.Status.BAD_REQUEST,
        String.format(
            "Exporter '%s' of provider type '%s' is not allowed for %s export on universe '%s'.",
            provider.getName(), providerType, signal, universe.getUniverseUUID()));
  }

  /**
   * Rejects an export feature that needs the spec.config passthrough chart on a K8s universe still
   * running a pre-passthrough version. No-op when the feature is disabled or the version supports
   * passthrough. {@code feature} is the human-readable label (e.g. "Query log export").
   */
  private void requirePassthroughForK8s(
      boolean supportsPassthrough,
      boolean featureEnabled,
      String feature,
      Universe universe,
      UserIntent userIntent) {
    if (featureEnabled && !supportsPassthrough) {
      throw new PlatformServiceException(
          play.mvc.Http.Status.BAD_REQUEST,
          String.format(
              "%s is not supported for kubernetes universe '%s' running version '%s'. Please"
                  + " upgrade to version '%s' or '%s', or disable %s.",
              feature,
              universe.getUniverseUUID(),
              userIntent.ybSoftwareVersion,
              OtelCollectorUtil.OTEL_HELM_CONFIG_PASSTHROUGH_STABLE_VERSION,
              OtelCollectorUtil.OTEL_HELM_CONFIG_PASSTHROUGH_PREVIEW_VERSION,
              feature.toLowerCase(Locale.ROOT)));
    }
  }

  public static class Converter extends BaseConverter<ExportTelemetryConfigParams> {}
}
