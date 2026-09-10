// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.utils;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.audit.otel.OtelCollectorUtil;
import com.yugabyte.yw.common.export.ExportTelemetryConfigMapper;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.exporters.UniverseExporterConfig;
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
import com.yugabyte.yw.models.helpers.exporters.server.ServerLogLevel;
import com.yugabyte.yw.models.helpers.exporters.server.TServerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.UniverseServerLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.server.YsqlConnMgrLogConfig;
import io.yugabyte.operator.v1alpha1.ybuniversespec.Telemetry;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.AuditLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.ControllerLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.MasterLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.Metrics;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.QueryLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.TserverLogs;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.YsqlConnMgrLogs;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import javax.annotation.Nullable;
import org.apache.commons.collections4.CollectionUtils;

/**
 * Converts the {@code spec.telemetry} block of a {@code YBUniverse} custom resource into the
 * internal {@link TelemetryConfig} the export-telemetry task takes, resolving every exporter's
 * {@code telemetryProvider} CR name to the YBA telemetry provider UUID.
 *
 * <p><b>Normalization is the whole point of this class.</b> The config produced here is compared
 * against the universe's currently applied config to decide whether to dispatch a task (see {@link
 * OperatorUtils#shouldUpdateTelemetry}).
 *
 * <p>The way that is guaranteed here is by reusing the REST path itself rather than re-deriving
 * those fields.
 *
 * <p>{@code scrapeConfigTargets} is handled specially: we can omit vm-specific targets for k8s.
 */
public class UniverseTelemetrySpecConverter {

  /**
   * Copies CR sub-objects onto their internal counterparts. The CRD property names match the
   * internal field names exactly (camelCase on both sides),
   */
  private static final ObjectMapper CR_MAPPER =
      new ObjectMapper().configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false);

  /** Resolves a {@code TelemetryProvider} CR name to the YBA telemetry provider UUID. */
  @FunctionalInterface
  public interface ExporterUuidResolver {
    /**
     * @param telemetryProviderCrName the TelemetryProvider CR name from the universe CR
     * @return the YBA UUID of that provider
     * @throws Exception if the CR is missing, not ready, or has no resolved UUID yet
     */
    UUID resolve(String telemetryProviderCrName) throws Exception;
  }

  private UniverseTelemetrySpecConverter() {}

  /**
   * The telemetry config a universe CR asks for. A null (or empty) telemetry block yields a config
   * with every section null, which is how "no exports configured" is expressed.
   *
   * @param telemetry the CR's spec.telemetry block, may be null
   * @param resolver resolves each exporter's TelemetryProvider CR name to a YBA UUID
   * @return the desired config, normalized exactly as the unified REST API would normalize it
   * @throws Exception if an exporter references a TelemetryProvider CR that is not usable yet
   */
  public static TelemetryConfig toTelemetryConfig(
      @Nullable Telemetry telemetry, ExporterUuidResolver resolver) throws Exception {
    if (telemetry == null) {
      return new TelemetryConfig();
    }
    TelemetryConfig authored =
        TelemetryConfig.builder()
            .auditLogConfig(toAuditLogConfig(telemetry.getAuditLogs(), resolver))
            .queryLogConfig(toQueryLogConfig(telemetry.getQueryLogs(), resolver))
            .metricsExportConfig(toMetricsExportConfig(telemetry.getMetrics(), resolver))
            .masterLogConfig(toMasterLogConfig(telemetry.getMasterLogs(), resolver))
            .tserverLogConfig(toTserverLogConfig(telemetry.getTserverLogs(), resolver))
            .ysqlConnMgrLogConfig(toYsqlConnMgrLogConfig(telemetry.getYsqlConnMgrLogs(), resolver))
            .controllerLogConfig(toControllerLogConfig(telemetry.getControllerLogs(), resolver))
            .build();
    return normalizeThroughSharedMapper(authored);
  }

  private static TelemetryConfig normalizeThroughSharedMapper(TelemetryConfig authored) {
    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    ExportTelemetryConfigMapper.fillParams(
        ExportTelemetryConfigMapper.toGenerated(authored), params);
    return params.getTelemetryConfig();
  }

  @Nullable
  private static AuditLogConfig toAuditLogConfig(
      @Nullable AuditLogs crAuditLogs, ExporterUuidResolver resolver) throws Exception {
    if (crAuditLogs == null) {
      return null;
    }
    AuditLogConfig config = new AuditLogConfig();
    // The 'enabled' flag on each sub-config is not authored in the CR: presence of the sub-object
    // means that audit source is on, and the shared mapper force-sets enabled=true for it.
    if (crAuditLogs.getYsqlAuditConfig() != null) {
      config.setYsqlAuditConfig(
          CR_MAPPER.convertValue(crAuditLogs.getYsqlAuditConfig(), YSQLAuditConfig.class));
    }
    if (crAuditLogs.getYcqlAuditConfig() != null) {
      config.setYcqlAuditConfig(
          CR_MAPPER.convertValue(crAuditLogs.getYcqlAuditConfig(), YCQLAuditConfig.class));
    }
    config.setUniverseLogsExporterConfig(
        toExporters(
            crAuditLogs.getExporters(),
            io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.auditlogs.Exporters
                ::getTelemetryProvider,
            UniverseLogsExporterConfig.class,
            resolver));
    return config;
  }

  @Nullable
  private static QueryLogConfig toQueryLogConfig(
      @Nullable QueryLogs crQueryLogs, ExporterUuidResolver resolver) throws Exception {
    if (crQueryLogs == null) {
      return null;
    }
    QueryLogConfig config = new QueryLogConfig();
    if (crQueryLogs.getYsqlQueryLogConfig() != null) {
      config.setYsqlQueryLogConfig(
          CR_MAPPER.convertValue(crQueryLogs.getYsqlQueryLogConfig(), YSQLQueryLogConfig.class));
    }
    config.setUniverseLogsExporterConfig(
        toExporters(
            crQueryLogs.getExporters(),
            io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.querylogs.Exporters
                ::getTelemetryProvider,
            UniverseQueryLogsExporterConfig.class,
            resolver));
    return config;
  }

  @Nullable
  private static MetricsExportConfig toMetricsExportConfig(
      @Nullable Metrics crMetrics, ExporterUuidResolver resolver) throws Exception {
    if (crMetrics == null) {
      return null;
    }
    MetricsExportConfig config = new MetricsExportConfig();
    if (crMetrics.getScrapeIntervalSeconds() != null) {
      config.setScrapeIntervalSeconds(crMetrics.getScrapeIntervalSeconds().intValue());
    }
    if (crMetrics.getScrapeTimeoutSeconds() != null) {
      config.setScrapeTimeoutSeconds(crMetrics.getScrapeTimeoutSeconds().intValue());
    }
    if (crMetrics.getCollectionLevel() != null) {
      config.setCollectionLevel(
          MetricCollectionLevel.valueOf(crMetrics.getCollectionLevel().getValue()));
    }
    config.setScrapeConfigTargets(scrapeConfigTargets(crMetrics));
    config.setUniverseMetricsExporterConfig(
        toExporters(
            crMetrics.getExporters(),
            io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.metrics.Exporters
                ::getTelemetryProvider,
            UniverseMetricsExporterConfig.class,
            resolver));
    return config;
  }

  /**
   * The targets to scrape, defaulting to {@link OtelCollectorUtil#K8S_SUPPORTED_SCRAPE_TARGETS}
   * rather than to the shared mapper's {@code EnumSet.allOf}, which would add the VM-only
   * NODE_EXPORT and NODE_AGENT_EXPORT targets that a Kubernetes universe rejects. The CRD enum
   * already restricts the values that can be written, but it supplies no value when the field is
   * omitted, so the default is still needed here.
   */
  private static Set<ScrapeConfigTargetType> scrapeConfigTargets(Metrics crMetrics) {
    Set<ScrapeConfigTargetType> targets = EnumSet.noneOf(ScrapeConfigTargetType.class);
    if (CollectionUtils.isEmpty(crMetrics.getScrapeConfigTargets())) {
      targets.addAll(OtelCollectorUtil.K8S_SUPPORTED_SCRAPE_TARGETS);
    } else {
      crMetrics
          .getScrapeConfigTargets()
          .forEach(t -> targets.add(ScrapeConfigTargetType.valueOf(t.getValue())));
    }
    return targets;
  }

  @Nullable
  private static MasterLogConfig toMasterLogConfig(
      @Nullable MasterLogs crMasterLogs, ExporterUuidResolver resolver) throws Exception {
    if (crMasterLogs == null) {
      return null;
    }
    MasterLogConfig config = new MasterLogConfig();
    if (crMasterLogs.getMinLevel() != null) {
      config.setMinLevel(ServerLogLevel.valueOf(crMasterLogs.getMinLevel().getValue()));
    }
    if (crMasterLogs.getNoiseSampleDropRatio() != null) {
      config.setNoiseSampleDropRatio(crMasterLogs.getNoiseSampleDropRatio());
    }
    config.setUniverseLogsExporterConfig(
        toExporters(
            crMasterLogs.getExporters(),
            io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.masterlogs.Exporters
                ::getTelemetryProvider,
            UniverseServerLogsExporterConfig.class,
            resolver));
    return config;
  }

  @Nullable
  private static TServerLogConfig toTserverLogConfig(
      @Nullable TserverLogs crTserverLogs, ExporterUuidResolver resolver) throws Exception {
    if (crTserverLogs == null) {
      return null;
    }
    TServerLogConfig config = new TServerLogConfig();
    if (crTserverLogs.getMinLevel() != null) {
      config.setMinLevel(ServerLogLevel.valueOf(crTserverLogs.getMinLevel().getValue()));
    }
    config.setUniverseLogsExporterConfig(
        toExporters(
            crTserverLogs.getExporters(),
            io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.tserverlogs.Exporters
                ::getTelemetryProvider,
            UniverseServerLogsExporterConfig.class,
            resolver));
    return config;
  }

  /**
   * YSQL Connection Manager logs: an exporters-only section (no severity filter, the whole file is
   * tailed), so there is nothing to convert beyond the exporter list.
   */
  @Nullable
  private static YsqlConnMgrLogConfig toYsqlConnMgrLogConfig(
      @Nullable YsqlConnMgrLogs crYsqlConnMgrLogs, ExporterUuidResolver resolver) throws Exception {
    if (crYsqlConnMgrLogs == null) {
      return null;
    }
    YsqlConnMgrLogConfig config = new YsqlConnMgrLogConfig();
    config.setUniverseLogsExporterConfig(
        toExporters(
            crYsqlConnMgrLogs.getExporters(),
            io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.ysqlconnmgrlogs.Exporters
                ::getTelemetryProvider,
            UniverseServerLogsExporterConfig.class,
            resolver));
    return config;
  }

  /** YB-Controller logs: exporters-only, like YSQL Connection Manager logs. */
  @Nullable
  private static ControllerLogConfig toControllerLogConfig(
      @Nullable ControllerLogs crControllerLogs, ExporterUuidResolver resolver) throws Exception {
    if (crControllerLogs == null) {
      return null;
    }
    ControllerLogConfig config = new ControllerLogConfig();
    config.setUniverseLogsExporterConfig(
        toExporters(
            crControllerLogs.getExporters(),
            io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.controllerlogs.Exporters
                ::getTelemetryProvider,
            UniverseServerLogsExporterConfig.class,
            resolver));
    return config;
  }

  /**
   * Converts CR exporter items to their internal counterparts: a field-for-field Jackson copy (the
   * CRD names the batching and tag properties after the internal fields) plus the resolved exporter
   * UUID, which is the one field the manifest cannot carry.
   */
  private static <C, T extends UniverseExporterConfig> List<T> toExporters(
      @Nullable List<C> crExporters,
      Function<C, String> telemetryProviderGetter,
      Class<T> internalClass,
      ExporterUuidResolver resolver)
      throws Exception {
    List<T> exporters = new ArrayList<>();
    if (CollectionUtils.isEmpty(crExporters)) {
      return exporters;
    }
    for (C crExporter : crExporters) {
      T exporter = CR_MAPPER.convertValue(crExporter, internalClass);
      exporter.setExporterUuid(resolver.resolve(telemetryProviderGetter.apply(crExporter)));
      exporters.add(exporter);
    }
    return exporters;
  }
}
