package com.yugabyte.yw.common.audit.otel;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.yaml.SkipNullRepresenter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.audit.YCQLAuditConfig;
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
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.yaml.snakeyaml.Yaml;
import play.Environment;

@Singleton
public class OtelCollectorConfigGenerator {
  private final FileHelperService fileHelperService;
  private final TelemetryProviderService telemetryProviderService;

  private final AuditLogRegexGenerator auditLogRegexGenerator;

  @Inject
  public OtelCollectorConfigGenerator(
      Environment environment,
      FileHelperService fileHelperService,
      TelemetryProviderService telemetryProviderService,
      AuditLogRegexGenerator auditLogRegexGenerator) {
    this.fileHelperService = fileHelperService;
    this.telemetryProviderService = telemetryProviderService;
    this.auditLogRegexGenerator = auditLogRegexGenerator;
  }

  public Path generateConfigFile(
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      AuditLogConfig auditLogConfig,
      String logLinePrefix) {
    Path path =
        fileHelperService.createTempFile(
            "otel_collector_config_" + nodeParams.getUniverseUUID() + "_" + nodeParams.nodeUuid,
            ".yml");
    generateConfigFile(nodeParams, provider, userIntent, auditLogConfig, logLinePrefix, path);
    return path;
  }

  void generateConfigFile(
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      AuditLogConfig auditLogConfig,
      String logLinePrefix,
      Path path) {
    try (BufferedWriter writer = new BufferedWriter(new FileWriter(path.toFile()))) {
      Yaml yaml = new Yaml(new SkipNullRepresenter());
      OtelCollectorConfigFormat collectorConfigFormat = new OtelCollectorConfigFormat();
      // Receivers
      if (auditLogConfig.getYsqlAuditConfig() != null
          && auditLogConfig.getYsqlAuditConfig().isEnabled()) {
        collectorConfigFormat
            .getReceivers()
            .put("filelog/ysql", createYsqlReceiver(provider, logLinePrefix));
      }
      if (auditLogConfig.getYcqlAuditConfig() != null
          && auditLogConfig.getYcqlAuditConfig().isEnabled()) {
        collectorConfigFormat
            .getReceivers()
            .put("filelog/ycql", createYcqlReceiver(provider, auditLogConfig.getYcqlAuditConfig()));
      }

      // Extensions
      collectorConfigFormat
          .getExtensions()
          .put("file_storage/queue", createStorageExtension(provider, userIntent));

      // Service
      OtelCollectorConfigFormat.Service service = new OtelCollectorConfigFormat.Service();
      service.setExtensions(new ArrayList<>(collectorConfigFormat.getExtensions().keySet()));
      OtelCollectorConfigFormat.TelemetryConfig telemetryConfig =
          new OtelCollectorConfigFormat.TelemetryConfig();
      service.setTelemetry(telemetryConfig);
      OtelCollectorConfigFormat.LogsConfig logsConfig = new OtelCollectorConfigFormat.LogsConfig();
      telemetryConfig.setLogs(logsConfig);
      logsConfig.setOutput_paths(
          ImmutableList.of(provider.getYbHome() + "/otel-collector/logs/otel-collector.logs"));
      collectorConfigFormat.setService(service);

      // Exporters
      if (CollectionUtils.isNotEmpty(auditLogConfig.getUniverseLogsExporterConfig())) {
        List<String> currentProcessors =
            new ArrayList<>(collectorConfigFormat.getProcessors().keySet());
        auditLogConfig
            .getUniverseLogsExporterConfig()
            .forEach(
                config ->
                    appendExporter(
                        collectorConfigFormat, config, currentProcessors, nodeParams.nodeName));
      }

      yaml.dump(collectorConfigFormat, writer);
    } catch (Exception e) {
      throw new RuntimeException("Error creating OpenTelemetry collector config file", e);
    }
  }

  private OtelCollectorConfigFormat.Receiver createYsqlReceiver(
      Provider provider, String logPrefix) {
    AuditLogRegexGenerator.LogRegexResult regexResult =
        auditLogRegexGenerator.generateAuditLogRegex(logPrefix);
    // Filter only audit logs
    OtelCollectorConfigFormat.FilterOperator filterOperator =
        new OtelCollectorConfigFormat.FilterOperator();
    filterOperator.setType("filter");
    filterOperator.setExpr("body not matches \"^.*LOG:  AUDIT:.*$\"");

    // Parse attributes from audit logs
    OtelCollectorConfigFormat.RegexOperator regexOperator =
        new OtelCollectorConfigFormat.RegexOperator();
    regexOperator.setType("regex_parser");
    regexOperator.setRegex(regexResult.getRegex());
    regexOperator.setOn_error("drop");
    OtelCollectorConfigFormat.OperatorTimestamp timestamp =
        new OtelCollectorConfigFormat.OperatorTimestamp();
    if (regexResult
        .getTokens()
        .contains(AuditLogRegexGenerator.LogPrefixTokens.TIMESTAMP_WITHOUT_MS)) {
      timestamp.setParse_from("attributes.timestamp_without_ms");
      timestamp.setLayout("%Y-%m-%d %H:%M:%S %Z");
      regexOperator.setTimestamp(timestamp);
    } else if (regexResult
        .getTokens()
        .contains(AuditLogRegexGenerator.LogPrefixTokens.TIMESTAMP_WITH_MS)) {
      timestamp.setParse_from("attributes.timestamp_with_ms");
      timestamp.setLayout("%Y-%m-%d %H:%M:%S.%L %Z");
      regexOperator.setTimestamp(timestamp);
    } else if (regexResult
        .getTokens()
        .contains(AuditLogRegexGenerator.LogPrefixTokens.TIMESTAMP_WITH_MS_EPOCH)) {
      timestamp.setParse_from("attributes.timestamp_with_ms_epoch");
      timestamp.setLayout_type("epoch");
      timestamp.setLayout("s.ms");
      regexOperator.setTimestamp(timestamp);
    }
    OtelCollectorConfigFormat.FileLogReceiver receiver =
        createFileLogReceiver("ysql", ImmutableList.of(filterOperator, regexOperator));
    receiver.setInclude(ImmutableList.of(provider.getYbHome() + "/tserver/logs/postgresql-*.log"));
    receiver.setExclude(ImmutableList.of(provider.getYbHome() + "/tserver/logs/*.gz"));
    return receiver;
  }

  private OtelCollectorConfigFormat.Receiver createYcqlReceiver(
      Provider provider, YCQLAuditConfig config) {
    // Filter only audit logs
    OtelCollectorConfigFormat.FilterOperator filterOperator =
        new OtelCollectorConfigFormat.FilterOperator();
    filterOperator.setType("filter");
    filterOperator.setExpr("body not matches \"^.*AUDIT: user:.*$\"");

    // Parse attributes from audit logs
    OtelCollectorConfigFormat.RegexOperator regexOperator =
        new OtelCollectorConfigFormat.RegexOperator();
    regexOperator.setType("regex_parser");
    regexOperator.setRegex(
        "^((?P<log_level>\\w)(?P<log_time>\\d{2}\\d{2} \\d{2}:\\d{2}:\\d{2}[.]\\d{6})"
            + " (?P<thread_id>\\d+) (?P<file_name>[^:]+):(?P<file_line>\\d+)[]] AUDIT: "
            + "user:(?P<user_name>[^|]+)[|]host:(?P<local_host>[^:]+):(?P<local_port>\\d+)[|]"
            + "source:(?P<remote_host>[^|]+)[|]port:(?P<remote_port>\\d+)[|]"
            + "timestamp:(?P<timestamp>\\d+)[|]type:(?P<type>[^|]+)[|]"
            + "category:(?P<category>[^|]+)[|]ks:(?P<keyspace>[^|]+)[|]"
            + "scope:(?P<scope>[^|]+)[|]operation:(?P<statement>.*))|(.*)$");
    regexOperator.setOn_error("drop");
    OtelCollectorConfigFormat.OperatorTimestamp timestamp =
        new OtelCollectorConfigFormat.OperatorTimestamp();
    timestamp.setParse_from("attributes.timestamp");
    timestamp.setLayout_type("epoch");
    timestamp.setLayout("ms");
    regexOperator.setTimestamp(timestamp);
    OtelCollectorConfigFormat.FileLogReceiver receiver =
        createFileLogReceiver("ycql", ImmutableList.of(filterOperator, regexOperator));
    YCQLAuditConfig.YCQLAuditLogLevel logLevel =
        config.getLogLevel() != null
            ? config.getLogLevel()
            : YCQLAuditConfig.YCQLAuditLogLevel.ERROR;
    receiver.setInclude(
        ImmutableList.of(provider.getYbHome() + "/tserver/logs/yb-tserver.*." + logLevel + ".*"));
    receiver.setExclude(ImmutableList.of(provider.getYbHome() + "/tserver/logs/*.gz"));
    return receiver;
  }

  private OtelCollectorConfigFormat.FileLogReceiver createFileLogReceiver(
      String logType, List<OtelCollectorConfigFormat.Operator> operators) {
    OtelCollectorConfigFormat.FileLogReceiver receiver =
        new OtelCollectorConfigFormat.FileLogReceiver();
    receiver.setStart_at("beginning");
    receiver.setStorage("file_storage/queue");
    receiver.setOperators(operators);
    receiver.setAttributes(ImmutableMap.of("audit_log_type", logType));
    return receiver;
  }

  private void appendExporter(
      OtelCollectorConfigFormat collectorConfig,
      UniverseLogsExporterConfig logsExporterConfig,
      List<String> currentProcessors,
      String nodeName) {
    TelemetryProvider telemetryProvider =
        telemetryProviderService.getOrBadRequest(logsExporterConfig.getExporterUuid());
    Map<String, OtelCollectorConfigFormat.Exporter> exporters = collectorConfig.getExporters();
    String exporterName;
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
        exporterName = "datadog/" + telemetryProvider.getName();
        exporters.put(exporterName, setExporterCommonConfig(dataDogExporter, true, true));
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
        exporterName = "splunk_hec/" + telemetryProvider.getName();
        OtelCollectorConfigFormat.TlsSettings tlsSettings =
            new OtelCollectorConfigFormat.TlsSettings();
        tlsSettings.setInsecure_skip_verify(true);
        splunkExporter.setTls(tlsSettings);
        exporters.put(exporterName, setExporterCommonConfig(splunkExporter, true, true));
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
        exporterName = "awscloudwatchlogs/" + telemetryProvider.getName();
        exporters.put(exporterName, setExporterCommonConfig(awsCloudWatchExporter, false, true));
        break;
      case GCP_CLOUD_MONITORING:
        GCPCloudMonitoringConfig gcpCloudMonitoringConfig =
            (GCPCloudMonitoringConfig) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.GCPCloudMonitoringExporter gcpCloudMonitoringExporter =
            new OtelCollectorConfigFormat.GCPCloudMonitoringExporter();
        gcpCloudMonitoringExporter.setProject(gcpCloudMonitoringConfig.getProject());
        OtelCollectorConfigFormat.GCPCloudMonitoringLog log =
            new OtelCollectorConfigFormat.GCPCloudMonitoringLog();
        log.setDefault_log_name("YugabyteDB");
        gcpCloudMonitoringExporter.setLog(log);
        exporterName = "googlecloud/" + telemetryProvider.getName();
        // TODO add retry config to GCP provider once it's supported by Otel COllector
        exporters.put(
            exporterName, setExporterCommonConfig(gcpCloudMonitoringExporter, true, false));
        break;
      default:
        throw new IllegalArgumentException(
            "Exporter type "
                + telemetryProvider.getConfig().getType().name()
                + " is not supported.");
    }

    OtelCollectorConfigFormat.AttributesProcessor attributesProcessor =
        new OtelCollectorConfigFormat.AttributesProcessor();
    List<OtelCollectorConfigFormat.AttributeAction> attributeActions = new ArrayList<>();
    if (MapUtils.isNotEmpty(telemetryProvider.getTags())) {
      attributeActions.addAll(
          telemetryProvider.getTags().entrySet().stream()
              .map(
                  e ->
                      new OtelCollectorConfigFormat.AttributeAction(
                          e.getKey(), e.getValue(), "upsert"))
              .toList());
    }
    if (MapUtils.isNotEmpty(logsExporterConfig.getAdditionalTags())) {
      attributeActions.addAll(
          logsExporterConfig.getAdditionalTags().entrySet().stream()
              .map(
                  e ->
                      new OtelCollectorConfigFormat.AttributeAction(
                          e.getKey(), e.getValue(), "upsert"))
              .toList());
    }
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction("host.name", nodeName, "upsert"));
    attributesProcessor.setActions(attributeActions);

    String processorName = "attributes/" + telemetryProvider.getName();
    collectorConfig.getProcessors().put(processorName, attributesProcessor);
    List<String> processorNames = new ArrayList<>(currentProcessors);
    processorNames.add(processorName);

    OtelCollectorConfigFormat.Pipeline pipeline = new OtelCollectorConfigFormat.Pipeline();
    pipeline.setReceivers(new ArrayList<>(collectorConfig.getReceivers().keySet()));
    pipeline.setProcessors(processorNames);
    pipeline.setExporters(ImmutableList.of(exporterName));
    collectorConfig
        .getService()
        .getPipelines()
        .put("logs/" + telemetryProvider.getName(), pipeline);
  }

  private OtelCollectorConfigFormat.Exporter setExporterCommonConfig(
      OtelCollectorConfigFormat.Exporter exporter,
      boolean setQueueEnabled,
      boolean setRetryOnFailure) {
    if (setRetryOnFailure) {
      OtelCollectorConfigFormat.RetryConfig retryConfig =
          new OtelCollectorConfigFormat.RetryConfig();
      retryConfig.setEnabled(true);
      retryConfig.setInitial_interval("1m");
      retryConfig.setMax_interval("1800m");
      exporter.setRetry_on_failure(retryConfig);
    }
    OtelCollectorConfigFormat.QueueConfig queueConfig = new OtelCollectorConfigFormat.QueueConfig();
    if (setQueueEnabled) {
      queueConfig.setEnabled(true);
    }
    queueConfig.setStorage("file_storage/queue");
    exporter.setSending_queue(queueConfig);
    return exporter;
  }

  private OtelCollectorConfigFormat.Extension createStorageExtension(
      Provider provider, UniverseDefinitionTaskParams.UserIntent userIntent) {
    OtelCollectorConfigFormat.StorageExtension extension =
        new OtelCollectorConfigFormat.StorageExtension();
    extension.setDirectory(getFirstMountPoint(provider, userIntent) + "/otel-collector/queue");
    OtelCollectorConfigFormat.StorageCompaction compaction =
        new OtelCollectorConfigFormat.StorageCompaction();
    compaction.setDirectory(extension.getDirectory());
    compaction.setOn_start(true);
    compaction.setOn_rebound(true);
    compaction.setRebound_trigger_threshold_mib(10);
    compaction.setRebound_needed_threshold_mib(100);
    extension.setCompaction(compaction);
    return extension;
  }

  private String getFirstMountPoint(
      Provider provider, UniverseDefinitionTaskParams.UserIntent userIntent) {
    if (provider.getCloudCode() == Common.CloudType.onprem) {
      String mountPoints = userIntent.deviceInfo.mountPoints;
      return mountPoints.split(",")[0];
    }
    return "/mnt/d0";
  }
}
