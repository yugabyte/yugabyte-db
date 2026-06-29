package com.yugabyte.yw.common.audit.otel;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.ExportTelemetryConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class OtelCollectorUtil {

  private static final Logger LOG = LoggerFactory.getLogger(OtelCollectorUtil.class);

  // Version gate for the K8s otel chart contract. Charts at/after these versions accept the full
  // collector config via spec.config passthrough
  // (OtelCollectorConfigGenerator.getOtelColConfigK8s);
  // older charts assemble the config from structured Helm values and read the misspelled
  // "recievers" key (OtelCollectorConfigGenerator.getOtelHelmValues).
  public static final String OTEL_HELM_CONFIG_PASSTHROUGH_STABLE_VERSION = "2026.1.2.0";
  public static final String OTEL_HELM_CONFIG_PASSTHROUGH_PREVIEW_VERSION = "2.31.0.0";

  /**
   * Whether the chart for the given YBDB version accepts the full collector config via spec.config
   * passthrough. Versions at/after the gate use getOtelColConfigK8s; older versions fall back to
   * the structured getOtelHelmValues.
   */
  public static boolean supportsOtelConfigPassthrough(String ybSoftwareVersion) {
    if (ybSoftwareVersion == null) {
      return true;
    }
    return Util.compareYBVersions(
            ybSoftwareVersion,
            OTEL_HELM_CONFIG_PASSTHROUGH_STABLE_VERSION,
            OTEL_HELM_CONFIG_PASSTHROUGH_PREVIEW_VERSION,
            true)
        >= 0;
  }

  public static boolean isAuditLogEnabledInUniverse(AuditLogConfig config) {
    if (config == null) {
      return false;
    }
    return !((config != null)
        && ((config.getYsqlAuditConfig() == null || !config.getYsqlAuditConfig().isEnabled())
            && (config.getYcqlAuditConfig() == null || !config.getYcqlAuditConfig().isEnabled())));
  }

  public static boolean isYsqlAuditEnabled(AuditLogConfig config) {
    return config != null
        && config.getYsqlAuditConfig() != null
        && config.getYsqlAuditConfig().isEnabled();
  }

  public static boolean isYcqlAuditEnabled(AuditLogConfig config) {
    return config != null
        && config.getYcqlAuditConfig() != null
        && config.getYcqlAuditConfig().isEnabled();
  }

  public static boolean isQueryLogEnabledInUniverse(QueryLogConfig config) {
    if (config == null) {
      return false;
    }
    return !((config != null)
        && (config.getYsqlQueryLogConfig() == null || !config.getYsqlQueryLogConfig().isEnabled()));
  }

  public static boolean isAuditLogExportEnabledInUniverse(AuditLogConfig config) {
    return (config != null
        && config.isExportActive()
        && CollectionUtils.isNotEmpty(config.getUniverseLogsExporterConfig()));
  }

  public static boolean isQueryLogExportEnabledInUniverse(QueryLogConfig config) {
    return (config != null
        && config.isExportActive()
        && CollectionUtils.isNotEmpty(config.getUniverseLogsExporterConfig()));
  }

  public static boolean isMetricsExportEnabledInUniverse(MetricsExportConfig config) {
    return (config != null
        && config.isExportActive()
        && CollectionUtils.isNotEmpty(config.getUniverseMetricsExporterConfig()));
  }

  public static boolean isMasterLogExportEnabledInUniverse(MasterLogConfig config) {
    // isExportActive() is itself "has exporters", so no separate isNotEmpty check is needed
    // (unlike audit/query, whose exportActive is a separate stored flag).
    return config != null && config.isExportActive();
  }

  // --- Aggregate helpers over the whole TelemetryConfig. Adding a new export type updates only
  // these (and the per-type isXEnabledInUniverse helpers), not every call site. ---

  /**
   * True if any telemetry section exists and is enabled in the universe (logs gflag on / active).
   */
  public static boolean hasAnyTelemetryEnabledInUniverse(TelemetryConfig tc) {
    return tc != null
        && (isAuditLogEnabledInUniverse(tc.getAuditLogConfig())
            || isQueryLogEnabledInUniverse(tc.getQueryLogConfig())
            || isMetricsExportEnabledInUniverse(tc.getMetricsExportConfig())
            || isMasterLogExportEnabledInUniverse(tc.getMasterLogConfig()));
  }

  /** True if any telemetry section is actively exporting (export active and exporters present). */
  public static boolean isAnyExportEnabledInUniverse(TelemetryConfig tc) {
    return tc != null
        && (isAuditLogExportEnabledInUniverse(tc.getAuditLogConfig())
            || isQueryLogExportEnabledInUniverse(tc.getQueryLogConfig())
            || isMetricsExportEnabledInUniverse(tc.getMetricsExportConfig())
            || isMasterLogExportEnabledInUniverse(tc.getMasterLogConfig()));
  }

  /** Collects the exporter UUIDs of all actively-exporting telemetry sections. */
  public static Set<UUID> getActiveExporterUuids(TelemetryConfig tc) {
    Set<UUID> uuids = new HashSet<>();
    if (tc == null) {
      return uuids;
    }
    if (isAuditLogExportEnabledInUniverse(tc.getAuditLogConfig())) {
      tc.getAuditLogConfig()
          .getUniverseLogsExporterConfig()
          .forEach(c -> uuids.add(c.getExporterUuid()));
    }
    if (isQueryLogExportEnabledInUniverse(tc.getQueryLogConfig())) {
      tc.getQueryLogConfig()
          .getUniverseLogsExporterConfig()
          .forEach(c -> uuids.add(c.getExporterUuid()));
    }
    if (isMetricsExportEnabledInUniverse(tc.getMetricsExportConfig())) {
      tc.getMetricsExportConfig()
          .getUniverseMetricsExporterConfig()
          .forEach(c -> uuids.add(c.getExporterUuid()));
    }
    if (isMasterLogExportEnabledInUniverse(tc.getMasterLogConfig())) {
      tc.getMasterLogConfig()
          .getUniverseLogsExporterConfig()
          .forEach(c -> uuids.add(c.getExporterUuid()));
    }
    return uuids;
  }

  /**
   * The universe's current full telemetry config. The ExportTelemetryConfig table is the source of
   * truth, but audit/query/metrics are also mirrored into the primary cluster userIntent by paths
   * that do not update the table (e.g. edit universe), so the table row can lag userIntent for
   * those sections. To avoid silently dropping a still-configured section, this resolves each of
   * audit, query and metrics separately: use the table value when present, otherwise fall back to
   * the userIntent copy. Master logs (and any newer export type) live only in the table and get no
   * userIntent fallback. Every flow that needs the current config - the v1-compat modify shims and
   * the provision/upgrade re-apply paths - should use this and override only the section it
   * changes, so the table stays authoritative and a new export type rides along without touching
   * those callers.
   */
  public static TelemetryConfig getCurrentTelemetryConfig(Universe universe) {
    TelemetryConfig fromTable =
        ExportTelemetryConfig.getForUniverse(universe.getUniverseUUID())
            .map(ExportTelemetryConfig::getTelemetryConfig)
            .orElseGet(TelemetryConfig::new);
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    return TelemetryConfig.builder()
        .auditLogConfig(
            fromTable.getAuditLogConfig() != null
                ? fromTable.getAuditLogConfig()
                : userIntent.auditLogConfig)
        .queryLogConfig(
            fromTable.getQueryLogConfig() != null
                ? fromTable.getQueryLogConfig()
                : userIntent.queryLogConfig)
        .metricsExportConfig(
            fromTable.getMetricsExportConfig() != null
                ? fromTable.getMetricsExportConfig()
                : userIntent.metricsExportConfig)
        .masterLogConfig(fromTable.getMasterLogConfig())
        .build();
  }

  public static boolean yugabyteJobScrapeConfigEnabled(
      Set<ScrapeConfigTargetType> scrapeConfigTargets) {
    return scrapeConfigTargets.contains(ScrapeConfigTargetType.MASTER_EXPORT)
        || scrapeConfigTargets.contains(ScrapeConfigTargetType.TSERVER_EXPORT)
        || scrapeConfigTargets.contains(ScrapeConfigTargetType.YSQL_EXPORT)
        || scrapeConfigTargets.contains(ScrapeConfigTargetType.CQL_EXPORT);
  }
}
