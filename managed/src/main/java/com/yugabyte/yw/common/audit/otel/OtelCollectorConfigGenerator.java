package com.yugabyte.yw.common.audit.otel;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.audit.YSQLAuditConfig;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import com.yugabyte.yw.models.helpers.telemetry.GCPCloudMonitoringConfig;
import com.yugabyte.yw.models.helpers.telemetry.SplunkConfig;
import java.io.BufferedWriter;
import java.io.FileWriter;
import java.nio.file.Path;
import java.util.*;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.apache.commons.collections.CollectionUtils;
import org.yaml.snakeyaml.Yaml;
import play.Environment;

@Singleton
public class OtelCollectorConfigGenerator {
  private final Environment environment;
  private final FileHelperService fileHelperService;
  private final TelemetryProviderService telemetryProviderService;

  @Inject
  public OtelCollectorConfigGenerator(
      Environment environment,
      FileHelperService fileHelperService,
      TelemetryProviderService telemetryProviderService) {
    this.environment = environment;
    this.fileHelperService = fileHelperService;
    this.telemetryProviderService = telemetryProviderService;
  }

  public Path generateConfigFile(
      NodeTaskParams nodeParams, Provider provider, AuditLogConfig auditLogConfig) {
    Path path =
        fileHelperService.createTempFile(
            "otel_collector_config_" + nodeParams.getUniverseUUID() + "_" + nodeParams.nodeUuid,
            ".yml");
    try (BufferedWriter writer = new BufferedWriter(new FileWriter(path.toFile()))) {
      Yaml yaml = new Yaml();
      OtelCollectorConfigFormat collectorConfigFormat = new OtelCollectorConfigFormat();

      // Receivers
      if (auditLogConfig.getYsqlAuditConfig() != null
          && auditLogConfig.getYsqlAuditConfig().isEnabled()) {
        collectorConfigFormat
            .getReceivers()
            .put("filelog/ysql", createYsqlReceiver(provider, auditLogConfig.getYsqlAuditConfig()));
      }
      if (auditLogConfig.getYcqlAuditConfig() != null
          && auditLogConfig.getYcqlAuditConfig().isEnabled()) {
        collectorConfigFormat
            .getReceivers()
            .put("filelog/ycql", createYcqlReceiver(provider, auditLogConfig.getYcqlAuditConfig()));
      }

      // Exporters
      if (CollectionUtils.isNotEmpty(auditLogConfig.getUniverseLogsExporterConfig())) {
        auditLogConfig
            .getUniverseLogsExporterConfig()
            .forEach(config -> appendExporter(collectorConfigFormat.getExporters(), config));
      }

      // Extensions
      collectorConfigFormat
          .getExtensions()
          .put("file_storage/psq", createStorageExtension(provider));

      // Service
      OtelCollectorConfigFormat.Service service = new OtelCollectorConfigFormat.Service();
      service.setExtensions(new ArrayList<>(collectorConfigFormat.getExtensions().keySet()));
      OtelCollectorConfigFormat.Pipeline pipeline = new OtelCollectorConfigFormat.Pipeline();
      pipeline.setReceivers(new ArrayList<>(collectorConfigFormat.getReceivers().keySet()));
      pipeline.setProcessors(new ArrayList<>(collectorConfigFormat.getProcessors().keySet()));
      pipeline.setExporters(new ArrayList<>(collectorConfigFormat.getExporters().keySet()));
      service.getPipelines().put("logs", pipeline);
      OtelCollectorConfigFormat.TelemetryConfig telemetryConfig =
          new OtelCollectorConfigFormat.TelemetryConfig();
      service.setTelemetry(telemetryConfig);
      OtelCollectorConfigFormat.LogsConfig logsConfig = new OtelCollectorConfigFormat.LogsConfig();
      telemetryConfig.setLogs(logsConfig);
      logsConfig.setOutput_paths(
          ImmutableList.of(provider.getYbHome() + "/otel-collector/logs/otel-collector.logs"));
      collectorConfigFormat.setService(service);

      yaml.dump(collectorConfigFormat, writer);
    } catch (Exception e) {
      throw new RuntimeException("Error creating OpenTelemetry collector config file", e);
    }
    return path;
  }

  private OtelCollectorConfigFormat.Receiver createYsqlReceiver(
      Provider provider, YSQLAuditConfig ysqlAuditConfig) {
    OtelCollectorConfigFormat.FileLogReceiver receiver = createFileLogReceiver();
    receiver.setInclude(ImmutableList.of(provider.getYbHome() + "/tserver/logs/postgresql-*.log"));
    return receiver;
  }

  private OtelCollectorConfigFormat.Receiver createYcqlReceiver(
      Provider provider, YCQLAuditConfig ysqlAuditConfig) {
    OtelCollectorConfigFormat.FileLogReceiver receiver = createFileLogReceiver();
    receiver.setInclude(
        ImmutableList.of(provider.getYbHome() + "/tserver/logs/yb-tserver.*.WARNING.*"));
    return receiver;
  }

  private OtelCollectorConfigFormat.FileLogReceiver createFileLogReceiver() {
    OtelCollectorConfigFormat.FileLogReceiver receiver =
        new OtelCollectorConfigFormat.FileLogReceiver();
    receiver.setStart_at("beginning");
    receiver.setStorage("file_storage/psq");
    OtelCollectorConfigFormat.MultilineConfig multilineConfig =
        new OtelCollectorConfigFormat.MultilineConfig();
    multilineConfig.setLine_start_pattern(
        "[A-Z]\\d{4}\\s\\d{2}:\\d{2}:\\d{2}\\.\\d{6}\\s[\\d\\s]{5}\\s.*\\.cc:\\d+\\]");
    receiver.setMultiline(multilineConfig);
    receiver.setOperators(ImmutableList.of(/*TODO*/ ));
    return receiver;
  }

  private void appendExporter(
      Map<String, OtelCollectorConfigFormat.Exporter> exporters,
      UniverseLogsExporterConfig logsExporterConfig) {
    TelemetryProvider telemetryProvider =
        telemetryProviderService.getOrBadRequest(logsExporterConfig.getExporterUuid());
    switch (telemetryProvider.getConfig().getType()) {
      case DATA_DOG:
        DataDogConfig dataDogConfig = (DataDogConfig) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.DataDogExporter dataDogExporter =
            new OtelCollectorConfigFormat.DataDogExporter();
        OtelCollectorConfigFormat.DataDogApiConfig apiConfig =
            new OtelCollectorConfigFormat.DataDogApiConfig();
        apiConfig.setKey(dataDogConfig.getApiKey());
        apiConfig.setSite(dataDogConfig.getSite());
        dataDogExporter.setApi(apiConfig);
        exporters.put(
            "datadog/" + telemetryProvider.getName(), setExporterCommonConfig(dataDogExporter));
        break;
      case SPLUNK:
        SplunkConfig splunkConfig = (SplunkConfig) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.SplunkExporter splunkExporter =
            new OtelCollectorConfigFormat.SplunkExporter();
        splunkExporter.setToken(splunkConfig.getToken());
        splunkExporter.setEndpoint(splunkConfig.getEndpoint());
        splunkExporter.setSource(splunkConfig.getSource());
        splunkExporter.setSourcetype(splunkConfig.getSourceType());
        splunkExporter.setIndex(splunkConfig.getIndex());
        exporters.put(
            "splunkhec/" + telemetryProvider.getName(), setExporterCommonConfig(splunkExporter));
        break;
      case AWS_CLOUDWATCH:
        AWSCloudWatchConfig awsCloudWatchConfig =
            (AWSCloudWatchConfig) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.AWSCloudWatchExporter awsCloudWatchExporter =
            new OtelCollectorConfigFormat.AWSCloudWatchExporter();
        awsCloudWatchExporter.setEndpoint(awsCloudWatchConfig.getEndpoint());
        awsCloudWatchExporter.setRegion(awsCloudWatchConfig.getRegion());
        awsCloudWatchExporter.setLog_group_name(awsCloudWatchConfig.getLogGroup());
        awsCloudWatchExporter.setLog_stream_name(awsCloudWatchConfig.getLogStream());
        exporters.put("awscloudwatchlog/" + telemetryProvider.getName(), awsCloudWatchExporter);
        // TODO Pass credentials
        break;
      case GCP_CLOUD_MONITORING:
        GCPCloudMonitoringConfig gcpCloudMonitoringConfig =
            (GCPCloudMonitoringConfig) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.GCPCloudMonitoringExporter gcpCloudMonitoringExporter =
            new OtelCollectorConfigFormat.GCPCloudMonitoringExporter();
        gcpCloudMonitoringExporter.setProject(gcpCloudMonitoringConfig.getProject());
        exporters.put("googlecloud/" + telemetryProvider.getName(), gcpCloudMonitoringExporter);
        // TODO Pass credentials
        break;
    }
  }

  private OtelCollectorConfigFormat.Exporter setExporterCommonConfig(
      OtelCollectorConfigFormat.Exporter exporter) {
    OtelCollectorConfigFormat.RetryConfig retryConfig = new OtelCollectorConfigFormat.RetryConfig();
    retryConfig.setEnabled(true);
    retryConfig.setInitial_interval("1m");
    retryConfig.setMax_interval("1800m");
    exporter.setRetry_on_failure(retryConfig);
    OtelCollectorConfigFormat.QueueConfig queueConfig = new OtelCollectorConfigFormat.QueueConfig();
    queueConfig.setStorage("file_storage/psq");
    exporter.setSending_queue(queueConfig);
    return exporter;
  }

  private OtelCollectorConfigFormat.Extension createStorageExtension(Provider provider) {
    OtelCollectorConfigFormat.StorageExtension extension =
        new OtelCollectorConfigFormat.StorageExtension();
    extension.setDirectory(provider.getYbHome() + "/otel-collector/psq");
    OtelCollectorConfigFormat.StorageCompaction compaction =
        new OtelCollectorConfigFormat.StorageCompaction();
    compaction.setDirectory(extension.getDirectory());
    compaction.setOn_start(true);
    compaction.setOn_rebound(true);
    compaction.setRebound_trigger_threshold_mib(3);
    compaction.setRebound_needed_threshold_mib(5);
    extension.setCompaction(compaction);
    return extension;
  }
}
