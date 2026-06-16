// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import api.v2.models.AuditLogsTelemetrySpec;
import api.v2.models.ExportTelemetryUpgradeOptions;
import api.v2.models.MetricsTelemetrySpec;
import api.v2.models.QueryLogsTelemetrySpec;
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

/** Manual conversion between generated v2 API types and internal telemetry configs. */
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
      @Nullable List<api.v2.models.UniverseLogsExporterConfig> list) {
    return convertList(list, UniverseLogsExporterConfig.class);
  }

  private static List<UniverseQueryLogsExporterConfig>
      toUniverseQueryLogsExporterConfigsFromGenerated(
          @Nullable List<api.v2.models.UniverseQueryLogsExporterConfig> list) {
    return convertList(list, UniverseQueryLogsExporterConfig.class);
  }

  private static List<UniverseMetricsExporterConfig> toUniverseMetricsExporterConfigsFromGenerated(
      @Nullable List<api.v2.models.UniverseMetricsExporterConfig> list) {
    return convertList(list, UniverseMetricsExporterConfig.class);
  }

  // --- Conversion from internal types to generated api.v2.models types ---

  /**
   * Build a generated TelemetryConfig from the universe's currently applied configs (any of which
   * may be null to indicate that section is disabled).
   */
  public static api.v2.models.TelemetryConfig toGenerated(
      @Nullable AuditLogConfig auditLogConfig,
      @Nullable QueryLogConfig queryLogConfig,
      @Nullable MetricsExportConfig metricsExportConfig) {
    return new api.v2.models.TelemetryConfig()
        .auditLogs(toAuditLogsSpecFromInternal(auditLogConfig))
        .queryLogs(toQueryLogsSpecFromInternal(queryLogConfig))
        .metrics(toMetricsSpecFromInternal(metricsExportConfig));
  }

  @Nullable
  private static AuditLogsTelemetrySpec toAuditLogsSpecFromInternal(
      @Nullable AuditLogConfig config) {
    if (config == null) {
      return null;
    }
    AuditLogsTelemetrySpec spec = new AuditLogsTelemetrySpec();
    spec.setYsqlAuditConfig(
        MAPPER.convertValue(config.getYsqlAuditConfig(), api.v2.models.YSQLAuditConfig.class));
    spec.setYcqlAuditConfig(
        MAPPER.convertValue(config.getYcqlAuditConfig(), api.v2.models.YCQLAuditConfig.class));
    spec.setExporters(
        convertList(
            config.getUniverseLogsExporterConfig(),
            api.v2.models.UniverseLogsExporterConfig.class));
    return spec;
  }

  @Nullable
  private static QueryLogsTelemetrySpec toQueryLogsSpecFromInternal(
      @Nullable QueryLogConfig config) {
    if (config == null) {
      return null;
    }
    QueryLogsTelemetrySpec spec = new QueryLogsTelemetrySpec();
    spec.setYsqlQueryLogConfig(
        MAPPER.convertValue(
            config.getYsqlQueryLogConfig(), api.v2.models.YSQLQueryLogConfig.class));
    spec.setExporters(
        convertList(
            config.getUniverseLogsExporterConfig(),
            api.v2.models.UniverseQueryLogsExporterConfig.class));
    return spec;
  }

  @Nullable
  private static MetricsTelemetrySpec toMetricsSpecFromInternal(
      @Nullable MetricsExportConfig config) {
    if (config == null) {
      return null;
    }
    MetricsTelemetrySpec spec = new MetricsTelemetrySpec();
    spec.setScrapeIntervalSeconds(config.getScrapeIntervalSeconds());
    spec.setScrapeTimeoutSeconds(config.getScrapeTimeoutSeconds());
    if (config.getCollectionLevel() != null) {
      spec.setCollectionLevel(
          MetricsTelemetrySpec.CollectionLevelEnum.fromValue(
              config.getCollectionLevel().toString()));
    }
    if (config.getScrapeConfigTargets() != null) {
      spec.setScrapeConfigTargets(
          config.getScrapeConfigTargets().stream()
              .map(t -> api.v2.models.ScrapeConfigTargetType.fromValue(t.toString()))
              .collect(Collectors.toList()));
    }
    spec.setExporters(
        convertList(
            config.getUniverseMetricsExporterConfig(),
            api.v2.models.UniverseMetricsExporterConfig.class));
    return spec;
  }

  // The generated per-type exporter schemas (UniverseLogsExporterConfig etc.) share field names
  // with their internal counterparts, so Jackson converts each element directly in either
  // direction.
  private static <I, O> List<O> convertList(@Nullable List<I> list, Class<O> outClass) {
    if (list == null) {
      return Collections.emptyList();
    }
    return list.stream().map(c -> MAPPER.convertValue(c, outClass)).collect(Collectors.toList());
  }
}
