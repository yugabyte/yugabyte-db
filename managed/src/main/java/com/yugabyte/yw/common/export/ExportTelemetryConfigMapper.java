// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import api.v2.models.AuditLogsTelemetrySpec;
import api.v2.models.ExportTelemetryUpgradeOptions;
import api.v2.models.MetricsTelemetrySpec;
import api.v2.models.QueryLogsTelemetrySpec;
import api.v2.models.TelemetryExporterEntry;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YSQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.YSQLQueryLogConfig;
import java.util.Collections;
import java.util.EnumSet;
import java.util.List;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.apache.commons.collections4.CollectionUtils;

/** Manual conversion from generated API request. */
public class ExportTelemetryConfigMapper {

  private static final ObjectMapper MAPPER =
      new ObjectMapper().setPropertyNamingStrategy(PropertyNamingStrategies.SNAKE_CASE);

  /**
   * Fills the given params with telemetry configs converted from the generated API request body.
   * Null sections mean disabled; empty exporters list means enabled but no export. Caller must set
   * upgrade options on params as needed.
   */
  public static void fillParams(
      @Nullable api.v2.models.TelemetryConfig telemetryConfig, ExportTelemetryConfigParams params) {
    if (telemetryConfig == null) {
      params.setTelemetryConfig(null);
      return;
    }
    params.setTelemetryConfig(
        TelemetryConfig.of(
            toAuditLogConfigFromGenerated(telemetryConfig.getAuditLogs()),
            toQueryLogConfigFromGenerated(telemetryConfig.getQueryLogs()),
            toMetricsExportConfigFromGenerated(telemetryConfig.getMetrics())));
  }

  /**
   * Applies the generated upgrade options to the given params (rolling vs non-rolling, delays,
   * sleep after restart).
   */
  public static void applyUpgradeOptions(
      @Nullable ExportTelemetryUpgradeOptions upgradeOptions, ExportTelemetryConfigParams params) {
    if (upgradeOptions == null) {
      return;
    }
    Boolean rolling = upgradeOptions.getRollingUpgrade();
    if (rolling != null) {
      params.upgradeOption =
          Boolean.TRUE.equals(rolling)
              ? UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE
              : UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
    }
    if (upgradeOptions.getSleepAfterTserverRestartMillis() != null) {
      params.sleepAfterTServerRestartMillis = upgradeOptions.getSleepAfterTserverRestartMillis();
    }
    if (upgradeOptions.getSleepAfterMasterRestartMillis() != null) {
      params.sleepAfterMasterRestartMillis = upgradeOptions.getSleepAfterMasterRestartMillis();
    }
    if (upgradeOptions.getDelayBetweenMasterServers() != null) {
      params.delayBetweenMasterServers = upgradeOptions.getDelayBetweenMasterServers();
    }
    if (upgradeOptions.getDelayBetweenTserverServers() != null) {
      params.delayBetweenTserverServers = upgradeOptions.getDelayBetweenTserverServers();
    }
  }

  // --- Conversion from generated api.v2.models types ---

  @Nullable
  private static AuditLogConfig toAuditLogConfigFromGenerated(
      @Nullable AuditLogsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    AuditLogConfig config = new AuditLogConfig();
    if (spec.getYsqlAuditConfig() != null) {
      YSQLAuditConfig ysql = MAPPER.convertValue(spec.getYsqlAuditConfig(), YSQLAuditConfig.class);
      ysql.setEnabled(true);
      config.setYsqlAuditConfig(ysql);
    }
    if (spec.getYcqlAuditConfig() != null) {
      YCQLAuditConfig ycql = MAPPER.convertValue(spec.getYcqlAuditConfig(), YCQLAuditConfig.class);
      ycql.setEnabled(true);
      config.setYcqlAuditConfig(ycql);
    }
    List<UniverseLogsExporterConfig> exporters =
        toUniverseLogsExporterConfigsFromGenerated(spec.getExporters());
    config.setUniverseLogsExporterConfig(exporters != null ? exporters : Collections.emptyList());
    config.setExportActive(CollectionUtils.isNotEmpty(exporters));
    return config;
  }

  @Nullable
  private static QueryLogConfig toQueryLogConfigFromGenerated(
      @Nullable QueryLogsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    QueryLogConfig config = new QueryLogConfig();
    if (spec.getYsqlQueryLogConfig() != null) {
      YSQLQueryLogConfig ysql =
          MAPPER.convertValue(spec.getYsqlQueryLogConfig(), YSQLQueryLogConfig.class);
      ysql.setEnabled(true);
      config.setYsqlQueryLogConfig(ysql);
    }
    List<UniverseQueryLogsExporterConfig> exporters =
        toUniverseQueryLogsExporterConfigsFromGenerated(spec.getExporters());
    config.setUniverseLogsExporterConfig(exporters != null ? exporters : Collections.emptyList());
    config.setExportActive(CollectionUtils.isNotEmpty(exporters));
    return config;
  }

  @Nullable
  private static MetricsExportConfig toMetricsExportConfigFromGenerated(
      @Nullable MetricsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    MetricsExportConfig config = new MetricsExportConfig();
    config.setScrapeIntervalSeconds(
        spec.getScrapeIntervalSeconds() != null ? spec.getScrapeIntervalSeconds() : 30);
    config.setScrapeTimeoutSeconds(
        spec.getScrapeTimeoutSeconds() != null ? spec.getScrapeTimeoutSeconds() : 20);
    config.setCollectionLevel(
        spec.getCollectionLevel() != null
            ? MetricCollectionLevel.valueOf(spec.getCollectionLevel().toString())
            : MetricCollectionLevel.NORMAL);
    List<api.v2.models.ScrapeConfigTargetType> rawTargets = spec.getScrapeConfigTargets();
    if (rawTargets != null && !rawTargets.isEmpty()) {
      config.setScrapeConfigTargets(
          EnumSet.copyOf(
              rawTargets.stream()
                  .map(t -> ScrapeConfigTargetType.valueOf(t.toString()))
                  .collect(Collectors.toSet())));
    } else {
      config.setScrapeConfigTargets(EnumSet.allOf(ScrapeConfigTargetType.class));
    }
    List<UniverseMetricsExporterConfig> exporters =
        toUniverseMetricsExporterConfigsFromGenerated(spec.getExporters());
    config.setUniverseMetricsExporterConfig(
        exporters != null ? exporters : Collections.emptyList());
    return config;
  }

  private static List<UniverseLogsExporterConfig> toUniverseLogsExporterConfigsFromGenerated(
      @Nullable List<TelemetryExporterEntry> list) {
    if (list == null) return Collections.emptyList();
    return list.stream()
        .filter(e -> e.getExporterUuid() != null)
        .map(ExportTelemetryConfigMapper::toUniverseLogsExporterConfigFromGenerated)
        .collect(Collectors.toList());
  }

  private static UniverseLogsExporterConfig toUniverseLogsExporterConfigFromGenerated(
      TelemetryExporterEntry e) {
    UniverseLogsExporterConfig c = new UniverseLogsExporterConfig();
    c.setExporterUuid(e.getExporterUuid());
    c.setAdditionalTags(
        e.getAdditionalTags() != null ? e.getAdditionalTags() : Collections.emptyMap());
    return c;
  }

  private static List<UniverseQueryLogsExporterConfig>
      toUniverseQueryLogsExporterConfigsFromGenerated(@Nullable List<TelemetryExporterEntry> list) {
    if (list == null) return Collections.emptyList();
    return list.stream()
        .filter(e -> e.getExporterUuid() != null)
        .map(ExportTelemetryConfigMapper::toUniverseQueryLogsExporterConfigFromGenerated)
        .collect(Collectors.toList());
  }

  private static UniverseQueryLogsExporterConfig toUniverseQueryLogsExporterConfigFromGenerated(
      TelemetryExporterEntry e) {
    UniverseQueryLogsExporterConfig c = new UniverseQueryLogsExporterConfig();
    c.setExporterUuid(e.getExporterUuid());
    c.setAdditionalTags(
        e.getAdditionalTags() != null ? e.getAdditionalTags() : Collections.emptyMap());
    if (e.getSendBatchMaxSize() != null) c.setSendBatchMaxSize(e.getSendBatchMaxSize());
    if (e.getSendBatchSize() != null) c.setSendBatchSize(e.getSendBatchSize());
    if (e.getSendBatchTimeoutSeconds() != null)
      c.setSendBatchTimeoutSeconds(e.getSendBatchTimeoutSeconds());
    if (e.getMemoryLimitMib() != null) c.setMemoryLimitMib(e.getMemoryLimitMib());
    if (e.getMemoryLimitCheckIntervalSeconds() != null)
      c.setMemoryLimitCheckIntervalSeconds(e.getMemoryLimitCheckIntervalSeconds());
    return c;
  }

  private static List<UniverseMetricsExporterConfig> toUniverseMetricsExporterConfigsFromGenerated(
      @Nullable List<TelemetryExporterEntry> list) {
    if (list == null) return Collections.emptyList();
    return list.stream()
        .filter(e -> e.getExporterUuid() != null)
        .map(ExportTelemetryConfigMapper::toUniverseMetricsExporterConfigFromGenerated)
        .collect(Collectors.toList());
  }

  private static UniverseMetricsExporterConfig toUniverseMetricsExporterConfigFromGenerated(
      TelemetryExporterEntry e) {
    UniverseMetricsExporterConfig c = new UniverseMetricsExporterConfig();
    c.setExporterUuid(e.getExporterUuid());
    c.setAdditionalTags(
        e.getAdditionalTags() != null ? e.getAdditionalTags() : Collections.emptyMap());
    if (e.getSendBatchMaxSize() != null) c.setSendBatchMaxSize(e.getSendBatchMaxSize());
    if (e.getSendBatchSize() != null) c.setSendBatchSize(e.getSendBatchSize());
    if (e.getSendBatchTimeoutSeconds() != null)
      c.setSendBatchTimeoutSeconds(e.getSendBatchTimeoutSeconds());
    if (e.getMetricsPrefix() != null) c.setMetricsPrefix(e.getMetricsPrefix());
    if (e.getMemoryLimitMib() != null) c.setMemoryLimitMib(e.getMemoryLimitMib());
    if (e.getMemoryLimitCheckIntervalSeconds() != null)
      c.setMemoryLimitCheckIntervalSeconds(e.getMemoryLimitCheckIntervalSeconds());
    return c;
  }
}
