package com.yugabyte.yw.common.audit.otel;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import java.util.Set;
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

  public static boolean yugabyteJobScrapeConfigEnabled(
      Set<ScrapeConfigTargetType> scrapeConfigTargets) {
    return scrapeConfigTargets.contains(ScrapeConfigTargetType.MASTER_EXPORT)
        || scrapeConfigTargets.contains(ScrapeConfigTargetType.TSERVER_EXPORT)
        || scrapeConfigTargets.contains(ScrapeConfigTargetType.YSQL_EXPORT)
        || scrapeConfigTargets.contains(ScrapeConfigTargetType.CQL_EXPORT);
  }
}
