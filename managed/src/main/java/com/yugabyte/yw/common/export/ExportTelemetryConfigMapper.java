// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import api.v2.models.AuditLogsTelemetrySpec;
import api.v2.models.ControllerLogsTelemetrySpec;
import api.v2.models.ExportTelemetryUpgradeOptions;
import api.v2.models.MasterLogsTelemetrySpec;
import api.v2.models.MetricsTelemetrySpec;
import api.v2.models.NodeAgentLogsTelemetrySpec;
import api.v2.models.QueryLogsTelemetrySpec;
import api.v2.models.TServerLogsTelemetrySpec;
import api.v2.models.YnpLogsTelemetrySpec;
import api.v2.models.YsqlConnMgrLogsTelemetrySpec;
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
import com.yugabyte.yw.models.helpers.exporters.server.ControllerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.NodeAgentLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.ServerLogLevel;
import com.yugabyte.yw.models.helpers.exporters.server.TServerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.UniverseServerLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.server.YnpLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.YsqlConnMgrLogConfig;
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
        TelemetryConfig.builder()
            .auditLogConfig(toAuditLogConfigFromGenerated(telemetryConfig.getAuditLogs()))
            .queryLogConfig(toQueryLogConfigFromGenerated(telemetryConfig.getQueryLogs()))
            .metricsExportConfig(toMetricsExportConfigFromGenerated(telemetryConfig.getMetrics()))
            .masterLogConfig(toMasterLogConfigFromGenerated(telemetryConfig.getMasterLogs()))
            .tserverLogConfig(toTserverLogConfigFromGenerated(telemetryConfig.getTserverLogs()))
            .ysqlConnMgrLogConfig(
                toYsqlConnMgrLogConfigFromGenerated(telemetryConfig.getYsqlConnMgrLogs()))
            .nodeAgentLogConfig(
                toNodeAgentLogConfigFromGenerated(telemetryConfig.getNodeAgentLogs()))
            .ynpLogConfig(toYnpLogConfigFromGenerated(telemetryConfig.getYnpLogs()))
            .controllerLogConfig(
                toControllerLogConfigFromGenerated(telemetryConfig.getControllerLogs()))
            .build());
  }

  /** Shared exporters-only conversion for the internal diagnostic server-log specs. */
  @Nullable
  private static List<UniverseServerLogsExporterConfig> exportersFromGenerated(
      @Nullable List<api.v2.models.UniverseServerLogsExporterConfig> exporters) {
    List<UniverseServerLogsExporterConfig> converted =
        convertList(exporters, UniverseServerLogsExporterConfig.class);
    return converted != null ? converted : Collections.emptyList();
  }

  @Nullable
  private static YsqlConnMgrLogConfig toYsqlConnMgrLogConfigFromGenerated(
      @Nullable YsqlConnMgrLogsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    YsqlConnMgrLogConfig config = new YsqlConnMgrLogConfig();
    config.setUniverseLogsExporterConfig(exportersFromGenerated(spec.getExporters()));
    return config;
  }

  @Nullable
  private static NodeAgentLogConfig toNodeAgentLogConfigFromGenerated(
      @Nullable NodeAgentLogsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    NodeAgentLogConfig config = new NodeAgentLogConfig();
    config.setUniverseLogsExporterConfig(exportersFromGenerated(spec.getExporters()));
    return config;
  }

  @Nullable
  private static YnpLogConfig toYnpLogConfigFromGenerated(@Nullable YnpLogsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    YnpLogConfig config = new YnpLogConfig();
    config.setUniverseLogsExporterConfig(exportersFromGenerated(spec.getExporters()));
    return config;
  }

  @Nullable
  private static ControllerLogConfig toControllerLogConfigFromGenerated(
      @Nullable ControllerLogsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    ControllerLogConfig config = new ControllerLogConfig();
    config.setUniverseLogsExporterConfig(exportersFromGenerated(spec.getExporters()));
    return config;
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
      // Caller made an explicit choice; the handler must not downgrade it to a non-restart flow.
      params.setUpgradeOptionExplicitlySet(true);
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

  @Nullable
  private static MasterLogConfig toMasterLogConfigFromGenerated(
      @Nullable MasterLogsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    MasterLogConfig config = new MasterLogConfig();
    List<UniverseServerLogsExporterConfig> exporters =
        convertList(spec.getExporters(), UniverseServerLogsExporterConfig.class);
    config.setUniverseLogsExporterConfig(exporters != null ? exporters : Collections.emptyList());
    if (spec.getMinLevel() != null) {
      config.setMinLevel(ServerLogLevel.valueOf(spec.getMinLevel().name()));
    }
    if (spec.getNoiseSampleDropRatio() != null) {
      config.setNoiseSampleDropRatio(spec.getNoiseSampleDropRatio());
    }
    return config;
  }

  @Nullable
  private static TServerLogConfig toTserverLogConfigFromGenerated(
      @Nullable TServerLogsTelemetrySpec spec) {
    if (spec == null) {
      return null;
    }
    TServerLogConfig config = new TServerLogConfig();
    List<UniverseServerLogsExporterConfig> exporters =
        convertList(spec.getExporters(), UniverseServerLogsExporterConfig.class);
    config.setUniverseLogsExporterConfig(exporters != null ? exporters : Collections.emptyList());
    if (spec.getMinLevel() != null) {
      config.setMinLevel(ServerLogLevel.valueOf(spec.getMinLevel().name()));
    }
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
   * Build a generated TelemetryConfig from the universe's currently applied {@link TelemetryConfig}
   * (any section may be null to indicate it is disabled). Takes the whole aggregate so callers need
   * not change when a new export type is added; only a per-type conversion line below is added.
   */
  public static api.v2.models.TelemetryConfig toGenerated(
      @Nullable TelemetryConfig telemetryConfig) {
    if (telemetryConfig == null) {
      return new api.v2.models.TelemetryConfig();
    }
    return new api.v2.models.TelemetryConfig()
        .auditLogs(toAuditLogsSpecFromInternal(telemetryConfig.getAuditLogConfig()))
        .queryLogs(toQueryLogsSpecFromInternal(telemetryConfig.getQueryLogConfig()))
        .metrics(toMetricsSpecFromInternal(telemetryConfig.getMetricsExportConfig()))
        .masterLogs(toMasterLogsSpecFromInternal(telemetryConfig.getMasterLogConfig()))
        .tserverLogs(toTserverLogsSpecFromInternal(telemetryConfig.getTserverLogConfig()))
        .ysqlConnMgrLogs(
            toYsqlConnMgrLogsSpecFromInternal(telemetryConfig.getYsqlConnMgrLogConfig()))
        .nodeAgentLogs(toNodeAgentLogsSpecFromInternal(telemetryConfig.getNodeAgentLogConfig()))
        .ynpLogs(toYnpLogsSpecFromInternal(telemetryConfig.getYnpLogConfig()))
        .controllerLogs(toControllerLogsSpecFromInternal(telemetryConfig.getControllerLogConfig()));
  }

  /**
   * Shared exporters-only conversion back to the generated internal diagnostic server-log specs.
   */
  private static List<api.v2.models.UniverseServerLogsExporterConfig> exportersToGenerated(
      @Nullable List<UniverseServerLogsExporterConfig> exporters) {
    return convertList(exporters, api.v2.models.UniverseServerLogsExporterConfig.class);
  }

  @Nullable
  private static YsqlConnMgrLogsTelemetrySpec toYsqlConnMgrLogsSpecFromInternal(
      @Nullable YsqlConnMgrLogConfig config) {
    if (config == null) {
      return null;
    }
    YsqlConnMgrLogsTelemetrySpec spec = new YsqlConnMgrLogsTelemetrySpec();
    spec.setExporters(exportersToGenerated(config.getUniverseLogsExporterConfig()));
    return spec;
  }

  @Nullable
  private static NodeAgentLogsTelemetrySpec toNodeAgentLogsSpecFromInternal(
      @Nullable NodeAgentLogConfig config) {
    if (config == null) {
      return null;
    }
    NodeAgentLogsTelemetrySpec spec = new NodeAgentLogsTelemetrySpec();
    spec.setExporters(exportersToGenerated(config.getUniverseLogsExporterConfig()));
    return spec;
  }

  @Nullable
  private static YnpLogsTelemetrySpec toYnpLogsSpecFromInternal(@Nullable YnpLogConfig config) {
    if (config == null) {
      return null;
    }
    YnpLogsTelemetrySpec spec = new YnpLogsTelemetrySpec();
    spec.setExporters(exportersToGenerated(config.getUniverseLogsExporterConfig()));
    return spec;
  }

  @Nullable
  private static ControllerLogsTelemetrySpec toControllerLogsSpecFromInternal(
      @Nullable ControllerLogConfig config) {
    if (config == null) {
      return null;
    }
    ControllerLogsTelemetrySpec spec = new ControllerLogsTelemetrySpec();
    spec.setExporters(exportersToGenerated(config.getUniverseLogsExporterConfig()));
    return spec;
  }

  @Nullable
  private static MasterLogsTelemetrySpec toMasterLogsSpecFromInternal(
      @Nullable MasterLogConfig config) {
    if (config == null) {
      return null;
    }
    MasterLogsTelemetrySpec spec = new MasterLogsTelemetrySpec();
    spec.setExporters(
        convertList(
            config.getUniverseLogsExporterConfig(),
            api.v2.models.UniverseServerLogsExporterConfig.class));
    if (config.getMinLevel() != null) {
      spec.setMinLevel(MasterLogsTelemetrySpec.MinLevelEnum.fromValue(config.getMinLevel().name()));
    }
    if (config.getNoiseSampleDropRatio() != null) {
      spec.setNoiseSampleDropRatio(config.getNoiseSampleDropRatio());
    }
    return spec;
  }

  @Nullable
  private static TServerLogsTelemetrySpec toTserverLogsSpecFromInternal(
      @Nullable TServerLogConfig config) {
    if (config == null) {
      return null;
    }
    TServerLogsTelemetrySpec spec = new TServerLogsTelemetrySpec();
    spec.setExporters(
        convertList(
            config.getUniverseLogsExporterConfig(),
            api.v2.models.UniverseServerLogsExporterConfig.class));
    if (config.getMinLevel() != null) {
      spec.setMinLevel(
          TServerLogsTelemetrySpec.MinLevelEnum.fromValue(config.getMinLevel().name()));
    }
    return spec;
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
