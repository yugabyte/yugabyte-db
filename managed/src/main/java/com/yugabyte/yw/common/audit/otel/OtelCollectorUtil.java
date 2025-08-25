package com.yugabyte.yw.common.audit.otel;

import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class OtelCollectorUtil {

  private static final Logger LOG = LoggerFactory.getLogger(OtelCollectorUtil.class);

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
}
