package com.yugabyte.yw.common.audit.otel;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.audit.otel.OtelCollectorConfigFormat.MultilineConfig;
import com.yugabyte.yw.common.yaml.SkipNullRepresenter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import com.yugabyte.yw.models.helpers.telemetry.GCPCloudMonitoringConfig;
import com.yugabyte.yw.models.helpers.telemetry.LokiConfig;
import com.yugabyte.yw.models.helpers.telemetry.SplunkConfig;
import java.io.BufferedWriter;
import java.io.FileWriter;
import java.nio.file.Path;
import java.util.*;
import java.util.Base64;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.AllArgsConstructor;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
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
      MetricsExportConfig metricsExportConfig,
      String logLinePrefix,
      int otelColMetricsPort) {
    Path path =
        fileHelperService.createTempFile(
            "otel_collector_config_" + nodeParams.getUniverseUUID() + "_" + nodeParams.nodeUuid,
            ".yml");
    generateConfigFile(
        nodeParams,
        provider,
        userIntent,
        auditLogConfig,
        metricsExportConfig,
        logLinePrefix,
        path,
        otelColMetricsPort);
    return path;
  }

  void generateConfigFile(
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      AuditLogConfig auditLogConfig,
      MetricsExportConfig metricsExportConfig,
      String logLinePrefix,
      Path path,
      int otelColMetricsPort) {
    try (BufferedWriter writer = new BufferedWriter(new FileWriter(path.toFile()))) {
      Yaml yaml = new Yaml(new SkipNullRepresenter());
      OtelCollectorConfigFormat collectorConfigFormat = new OtelCollectorConfigFormat();
      addCommonService(collectorConfigFormat, provider, userIntent, otelColMetricsPort);
      if (auditLogConfig != null) {
        addAuditLogPipelines(
            collectorConfigFormat, nodeParams, provider, userIntent, auditLogConfig, logLinePrefix);
      }
      if (metricsExportConfig != null) {
        addMetricsExporterPipelines(
            collectorConfigFormat,
            nodeParams,
            provider,
            userIntent,
            metricsExportConfig,
            logLinePrefix);
      }

      yaml.dump(collectorConfigFormat, writer);
    } catch (Exception e) {
      throw new RuntimeException("Error creating OpenTelemetry collector config file", e);
    }
  }

  private void addCommonService(
      OtelCollectorConfigFormat collectorConfigFormat,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      int otelColMetricsPort) {
    OtelCollectorConfigFormat.Service service = new OtelCollectorConfigFormat.Service();

    // Add extensions
    collectorConfigFormat
        .getExtensions()
        .put("file_storage/queue", createStorageExtension(provider, userIntent));
    service.setExtensions(new ArrayList<>(collectorConfigFormat.getExtensions().keySet()));

    // Add internal telemetry config
    OtelCollectorConfigFormat.TelemetryConfig telemetryConfig =
        new OtelCollectorConfigFormat.TelemetryConfig();
    service.setTelemetry(telemetryConfig);

    // Add internal otel logs config
    OtelCollectorConfigFormat.LogsConfig logsConfig = new OtelCollectorConfigFormat.LogsConfig();
    logsConfig.setOutput_paths(
        ImmutableList.of(provider.getYbHome() + "/otel-collector/logs/otel-collector.logs"));
    telemetryConfig.setLogs(logsConfig);

    // Add internal otel metrics config
    OtelCollectorConfigFormat.MetricsConfig metricsConfig =
        new OtelCollectorConfigFormat.MetricsConfig();
    metricsConfig.setAddress("0.0.0.0:" + otelColMetricsPort);
    telemetryConfig.setMetrics(metricsConfig);

    // Add service to collector config
    collectorConfigFormat.setService(service);
  }

  private void addAuditLogPipelines(
      OtelCollectorConfigFormat collectorConfigFormat,
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      AuditLogConfig auditLogConfig,
      String logLinePrefix) {
    Customer customer = Customer.getOrBadRequest(provider.getCustomerUUID());
    Universe universe = Universe.getOrBadRequest(nodeParams.getUniverseUUID());
    // Receivers
    if (auditLogConfig != null
        && auditLogConfig.getYsqlAuditConfig() != null
        && auditLogConfig.getYsqlAuditConfig().isEnabled()) {
      collectorConfigFormat
          .getReceivers()
          .put("filelog/ysql", createYsqlReceiver(provider, logLinePrefix));
    }
    if (auditLogConfig != null
        && auditLogConfig.getYcqlAuditConfig() != null
        && auditLogConfig.getYcqlAuditConfig().isEnabled()) {
      collectorConfigFormat
          .getReceivers()
          .put("filelog/ycql", createYcqlReceiver(provider, auditLogConfig.getYcqlAuditConfig()));
    }

    // Exporters
    if (CollectionUtils.isNotEmpty(auditLogConfig.getUniverseLogsExporterConfig())) {
      List<String> currentProcessors =
          new ArrayList<>(collectorConfigFormat.getProcessors().keySet());
      auditLogConfig
          .getUniverseLogsExporterConfig()
          .forEach(
              config ->
                  appendExporter(
                      customer,
                      universe,
                      collectorConfigFormat,
                      config,
                      currentProcessors,
                      nodeParams.nodeName,
                      logLinePrefix));
    }
  }

  private void addMetricsExporterPipelines(
      OtelCollectorConfigFormat collectorConfigFormat,
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      MetricsExportConfig metricsExportConfig,
      String logLinePrefix) {
    if (metricsExportConfig != null
        && !CollectionUtils.isEmpty(metricsExportConfig.getUniverseMetricsExporterConfig())) {
      // Add a new pipeline for each active metrics exporter config, since they can have different
      // additional tags
      for (UniverseMetricsExporterConfig exporterConfig :
          metricsExportConfig.getUniverseMetricsExporterConfig()) {
        OtelCollectorConfigFormat.Pipeline pipeline = new OtelCollectorConfigFormat.Pipeline();
        // Add Prometheus receiver for metrics collection
        Universe universe = Universe.getOrBadRequest(nodeParams.getUniverseUUID());
        NodeDetails nodeDetails = universe.getNode(nodeParams.getNodeName());
        collectorConfigFormat
            .getReceivers()
            .put(
                "prometheus/yugabyte",
                createPrometheusReceiver(metricsExportConfig, nodeDetails.cloudInfo.private_ip));
        pipeline.setReceivers(ImmutableList.of("prometheus/yugabyte"));

        // Add AttributesProcessor for metrics
        collectorConfigFormat
            .getProcessors()
            .put(
                "attributes/metrics_export_" + exporterConfig.getExporterUuid(),
                createAttributesProcessor(exporterConfig));
        pipeline.setProcessors(
            ImmutableList.of("attributes/metrics_export_" + exporterConfig.getExporterUuid()));

        // Add metrics exporter for each active exporter config
        String exporterName = addMetricsExporter(collectorConfigFormat, exporterConfig);
        pipeline.setExporters(ImmutableList.of(exporterName));

        collectorConfigFormat
            .getService()
            .getPipelines()
            .put("metrics/" + exporterConfig.getExporterUuid(), pipeline);
      }
    }
  }

  public Map<String, Object> getOtelHelmValues(
      AuditLogConfig auditLogConfig, String logLinePrefix) {
    // For K8s, we don't need to generate the entire config file.
    // We just need exporterCofigs and receiverConfigs.
    Map<String, Object> otelConfig = new HashMap<>();

    if (!auditLogConfig.isExportActive()) {
      otelConfig.put("enabled", false);
    } else {

      otelConfig.put("enabled", true);
      // Recievers
      Map<String, Object> receivers = new HashMap<>();
      OtelCollectorConfigFormat.RegexOperator regexOperator = getRegexOperator(logLinePrefix);
      if (auditLogConfig.getYsqlAuditConfig() != null
          && auditLogConfig.getYsqlAuditConfig().isEnabled()) {
        Map<String, Object> ysqlReciever = new HashMap<>();
        ysqlReciever.put("regex", regexOperator.getRegex());
        ysqlReciever.put("timestamp", regexOperator.getTimestamp());
        ysqlReciever.put("lineStartPattern", generateLineStartPattern(logLinePrefix));
        receivers.put("ysql", ysqlReciever);
      }
      otelConfig.put("recievers", receivers);

      // Exporters
      OtelCollectorConfigFormat collectorConfigFormat = new OtelCollectorConfigFormat();
      List<Object> secretEnv = new ArrayList<>();
      if (CollectionUtils.isNotEmpty(auditLogConfig.getUniverseLogsExporterConfig())) {
        auditLogConfig
            .getUniverseLogsExporterConfig()
            .forEach(
                config -> {
                  TelemetryProvider telemetryProvider =
                      telemetryProviderService.getOrBadRequest(config.getExporterUuid());
                  String exporterName =
                      appendExporterConfig(
                          telemetryProvider,
                          collectorConfigFormat.getExporters(),
                          new ArrayList<>());
                  appendSecretEnv(telemetryProvider, secretEnv);
                });
        otelConfig.put("exporters", collectorConfigFormat.getExporters());
        otelConfig.put("secretEnv", secretEnv);
      }
    }
    return otelConfig;
  }

  private OtelCollectorConfigFormat.Receiver createYsqlReceiver(
      Provider provider, String logPrefix) {
    // Filter only single/multi-line audit logs
    OtelCollectorConfigFormat.FilterOperator filterOperator =
        new OtelCollectorConfigFormat.FilterOperator();
    filterOperator.setType("filter");
    filterOperator.setExpr("body not matches \"^.*\\\\w+:  AUDIT:(.|\\\\n|\\\\r|\\\\s)*$\"");

    // Parse attributes from audit logs
    OtelCollectorConfigFormat.RegexOperator regexOperator = getRegexOperator(logPrefix);

    OtelCollectorConfigFormat.FileLogReceiver receiver =
        createFileLogReceiver("ysql", ImmutableList.of(filterOperator, regexOperator));
    receiver.setInclude(ImmutableList.of(provider.getYbHome() + "/tserver/logs/postgresql-*.log"));
    receiver.setExclude(ImmutableList.of(provider.getYbHome() + "/tserver/logs/*.gz"));

    // Set multiline config to split by both normal log prefix and audit log prefix instead of the
    // default newline character.
    // Normal log line prefix = ([A-Z]\\d{4}). Ex: I0419 ...
    // Audit log line prefix = prefix of the audit log regex.
    // Example regex = ((?P<timestamp_with_ms>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}[.]\d{3} \w{3})[
    // ][[](?P<process_id>\d+)[]][ ])
    // Example audit log line = 2024-04-19 15:26:08.973 UTC [10011] LOG:  AUDIT: ...
    // Logic is we first split the lines by either normal log prefix or audit log prefix.
    // Then apply more strict audit log regex patterns to filter only the audit logs.
    MultilineConfig multilineConfig = new MultilineConfig();
    multilineConfig.setLine_start_pattern(generateLineStartPattern(logPrefix));
    receiver.setMultiline(multilineConfig);
    return receiver;
  }

  private OtelCollectorConfigFormat.Receiver createYcqlReceiver(
      Provider provider, YCQLAuditConfig config) {
    // Filter only audit logs
    OtelCollectorConfigFormat.FilterOperator filterOperator =
        new OtelCollectorConfigFormat.FilterOperator();
    filterOperator.setType("filter");
    filterOperator.setExpr("body not matches \"^.*AUDIT: user:(.|\\\\n|\\\\r|\\\\s)*$\"");

    // Parse attributes from audit logs
    OtelCollectorConfigFormat.RegexOperator regexOperator =
        new OtelCollectorConfigFormat.RegexOperator();
    regexOperator.setType("regex_parser");
    regexOperator.setRegex(
        "(?P<log_level>\\w)(?P<log_time>\\d{2}\\d{2} \\d{2}:\\d{2}:\\d{2}[.]\\d{6})"
            + "\\s*(?P<thread_id>\\d+) (?P<file_name>[^:]+):(?P<file_line>\\d+)[]] AUDIT: "
            + "user:(?P<user_name>[^|]+)[|]host:(?P<local_host>[^:]+):(?P<local_port>\\d+)[|]"
            + "source:(?P<remote_host>[^|]+)[|]port:(?P<remote_port>\\d+)[|]"
            + "timestamp:(?P<timestamp>\\d+)[|]type:(?P<type>[^|]+)[|]"
            + "category:(?P<category>[^|]+)([|]ks:(?P<keyspace>[^|]+))?([|]"
            + "scope:(?P<scope>[^|]+))?[|]operation:(?P<statement>(.|\\n|\\r|\\s)*)");
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
    // Set multiline config to split by both normal log prefix and audit log prefix.
    MultilineConfig multilineConfig = new MultilineConfig();
    multilineConfig.setLine_start_pattern("([A-Z]\\d{4})");
    receiver.setMultiline(multilineConfig);
    return receiver;
  }

  private OtelCollectorConfigFormat.FileLogReceiver createFileLogReceiver(
      String logType, List<OtelCollectorConfigFormat.Operator> operators) {
    OtelCollectorConfigFormat.FileLogReceiver receiver =
        new OtelCollectorConfigFormat.FileLogReceiver();
    receiver.setStart_at("beginning");
    receiver.setStorage("file_storage/queue");
    receiver.setOperators(operators);
    receiver.setAttributes(ImmutableMap.of("yugabyte.audit_log_type", logType));
    return receiver;
  }

  private OtelCollectorConfigFormat.PrometheusReceiver createPrometheusReceiver(
      MetricsExportConfig metricsExportConfig, String nodeAddress) {
    OtelCollectorConfigFormat.PrometheusReceiver receiver =
        new OtelCollectorConfigFormat.PrometheusReceiver();
    OtelCollectorConfigFormat.PrometheusConfig config =
        new OtelCollectorConfigFormat.PrometheusConfig();

    // Create scrape config for yugabyte level metrics.
    OtelCollectorConfigFormat.ScrapeConfig scrapeConfig =
        new OtelCollectorConfigFormat.ScrapeConfig();
    scrapeConfig.setJob_name("yugabyte");
    scrapeConfig.setScrape_interval(metricsExportConfig.getScrapeIntervalSeconds() + "s");
    scrapeConfig.setScrape_timeout(metricsExportConfig.getScrapeTimeoutSeconds() + "s");
    scrapeConfig.setScheme("http");
    scrapeConfig.setMetrics_path("/prometheus-metrics");

    // Create static config for master node.
    OtelCollectorConfigFormat.StaticConfig staticConfig = createMasterStaticConfig(nodeAddress);

    // TODO: Add static config for other services.
    scrapeConfig.setStatic_configs(ImmutableList.of(staticConfig));
    config.setScrape_configs(ImmutableList.of(scrapeConfig));
    receiver.setConfig(config);
    return receiver;
  }

  public OtelCollectorConfigFormat.StaticConfig createMasterStaticConfig(String nodeAddress) {
    OtelCollectorConfigFormat.StaticConfig staticConfig =
        new OtelCollectorConfigFormat.StaticConfig();
    staticConfig.setTargets(ImmutableList.of(nodeAddress + ":7000"));
    staticConfig.setLabels(ImmutableMap.of("export_type", "master_export"));
    staticConfig.setLabels(ImmutableMap.of("service", "yb-master"));
    return staticConfig;
  }

  private OtelCollectorConfigFormat.AttributesProcessor createAttributesProcessor(
      UniverseMetricsExporterConfig exporterConfig) {
    // Add additional tags from the exporter config.
    List<OtelCollectorConfigFormat.AttributeAction> attributeActions = new ArrayList<>();
    for (Map.Entry<String, String> entry : exporterConfig.getAdditionalTags().entrySet()) {
      attributeActions.add(
          new OtelCollectorConfigFormat.AttributeAction(
              entry.getKey(), entry.getValue(), "upsert", null));
    }

    OtelCollectorConfigFormat.AttributesProcessor processor =
        new OtelCollectorConfigFormat.AttributesProcessor();
    processor.setActions(attributeActions);
    return processor;
  }

  private String addMetricsExporter(
      OtelCollectorConfigFormat collectorConfigFormat,
      UniverseMetricsExporterConfig exporterConfig) {
    TelemetryProvider telemetryProvider =
        telemetryProviderService.getOrBadRequest(exporterConfig.getExporterUuid());

    String exporterName =
        appendExporterConfig(
            telemetryProvider, collectorConfigFormat.getExporters(), new ArrayList<>());

    return exporterName;
  }

  private OtelCollectorConfigFormat.RegexOperator getRegexOperator(String logPrefix) {
    AuditLogRegexGenerator.LogRegexResult regexResult =
        auditLogRegexGenerator.generateAuditLogRegex(logPrefix, /*onlyPrefix*/ false);
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
    return regexOperator;
  }

  private String generateLineStartPattern(String logPrefix) {
    return "([A-Z]\\d{4})|("
        + auditLogRegexGenerator.generateAuditLogRegex(logPrefix, /*onlyPrefix*/ true).getRegex()
        + ")";
  }

  @Data
  @AllArgsConstructor
  public static class RenamePair {
    private String before;
    private String after;

    private List<OtelCollectorConfigFormat.AttributeAction> getRenameAttributeActions() {
      List<OtelCollectorConfigFormat.AttributeAction> renameActionsList = new ArrayList<>();
      // Copy the attribute from existing attribute and delete the original one.
      renameActionsList.add(
          new OtelCollectorConfigFormat.AttributeAction(this.after, null, "upsert", this.before));
      renameActionsList.add(
          new OtelCollectorConfigFormat.AttributeAction(this.before, null, "delete", null));
      return renameActionsList;
    }
  }

  private void appendExporter(
      Customer customer,
      Universe universe,
      OtelCollectorConfigFormat collectorConfig,
      UniverseLogsExporterConfig logsExporterConfig,
      List<String> currentProcessors,
      String nodeName,
      String logLinePrefix) {
    NodeDetails nodeDetails = universe.getNode(nodeName);
    TelemetryProvider telemetryProvider =
        telemetryProviderService.getOrBadRequest(logsExporterConfig.getExporterUuid());
    Map<String, OtelCollectorConfigFormat.Exporter> exporters = collectorConfig.getExporters();
    String exporterName;
    List<OtelCollectorConfigFormat.AttributeAction> attributeActions = new ArrayList<>();
    OtelCollectorConfigFormat.AttributesProcessor attributesProcessor =
        new OtelCollectorConfigFormat.AttributesProcessor();

    exporterName = appendExporterConfig(telemetryProvider, exporters, attributeActions);

    // Add some common collector labels.
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction("host", nodeName, "upsert", null));
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction(
            "yugabyte.cloud",
            StringUtils.defaultString(nodeDetails.cloudInfo.cloud, ""),
            "upsert",
            null));
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction(
            "yugabyte.universe_uuid", universe.getUniverseUUID().toString(), "upsert", null));
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction(
            "yugabyte.node_type",
            universe.getCluster(nodeDetails.placementUuid).clusterType.toString(),
            "upsert",
            null));
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction(
            "yugabyte.region",
            StringUtils.defaultString(nodeDetails.cloudInfo.region, ""),
            "upsert",
            null));
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction(
            "yugabyte.zone",
            StringUtils.defaultString(nodeDetails.cloudInfo.az, ""),
            "upsert",
            null));
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction(
            "yugabyte.purpose",
            telemetryProvider.getConfig().getType().toString() + "_LOG_EXPORT",
            "upsert",
            null));

    // Rename the attributes to organise under the key yugabyte.
    List<RenamePair> renamePairs = new ArrayList<RenamePair>();
    renamePairs.add(new RenamePair("log.file.name", "yugabyte.log.file.name"));
    renamePairs.add(new RenamePair("log_level", "yugabyte.log_level"));
    renamePairs.add(new RenamePair("audit_type", "yugabyte.audit_type"));
    renamePairs.add(new RenamePair("statement_id", "yugabyte.statement_id"));
    renamePairs.add(new RenamePair("substatement_id", "yugabyte.substatement_id"));
    renamePairs.add(new RenamePair("class", "yugabyte.class"));
    renamePairs.add(new RenamePair("command", "yugabyte.command"));
    renamePairs.add(new RenamePair("object_type", "yugabyte.object_type"));
    renamePairs.add(new RenamePair("object_name", "yugabyte.object_name"));
    renamePairs.add(new RenamePair("statement", "yugabyte.statement"));
    renamePairs.forEach(rp -> attributeActions.addAll(rp.getRenameAttributeActions()));

    // Rename the log prefix extracted attributes to come under the key yugabyte.
    AuditLogRegexGenerator.LogRegexResult regexResult =
        auditLogRegexGenerator.generateAuditLogRegex(logLinePrefix, /*onlyPrefix*/ true);
    regexResult
        .getTokens()
        .forEach(
            token -> {
              RenamePair rp =
                  new RenamePair(token.getAttributeName(), token.getYugabyteAttributeName());
              attributeActions.addAll(rp.getRenameAttributeActions());
            });

    // Override or add tags from the exporter config.
    if (MapUtils.isNotEmpty(telemetryProvider.getTags())) {
      attributeActions.addAll(
          telemetryProvider.getTags().entrySet().stream()
              .map(
                  e ->
                      new OtelCollectorConfigFormat.AttributeAction(
                          e.getKey(), e.getValue(), "upsert", null))
              .toList());
    }

    // Override or add additional tags from the audit log config payload.
    if (MapUtils.isNotEmpty(logsExporterConfig.getAdditionalTags())) {
      attributeActions.addAll(
          logsExporterConfig.getAdditionalTags().entrySet().stream()
              .map(
                  e ->
                      new OtelCollectorConfigFormat.AttributeAction(
                          e.getKey(), e.getValue(), "upsert", null))
              .toList());
    }

    attributesProcessor.setActions(attributeActions);

    String processorName = "attributes/" + telemetryProvider.getUuid();
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
        .put("logs/" + telemetryProvider.getUuid(), pipeline);
  }

  private String appendExporterConfig(
      TelemetryProvider telemetryProvider,
      Map<String, OtelCollectorConfigFormat.Exporter> exporters,
      List<OtelCollectorConfigFormat.AttributeAction> attributeActions) {
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
        exporterName = "datadog/" + telemetryProvider.getUuid();
        exporters.put(exporterName, setExporterCommonConfig(dataDogExporter, true, true));

        // Add Datadog specific labels.
        attributeActions.add(
            new OtelCollectorConfigFormat.AttributeAction("ddsource", "yugabyte", "upsert", null));
        attributeActions.add(
            new OtelCollectorConfigFormat.AttributeAction(
                "service", "yb-otel-collector", "upsert", null));
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
        exporterName = "splunk_hec/" + telemetryProvider.getUuid();
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

        exporterName = "awscloudwatchlogs/" + telemetryProvider.getUuid();
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
        exporterName = "googlecloud/" + telemetryProvider.getUuid();
        // TODO add retry config to GCP provider once it's supported by Otel COllector
        exporters.put(
            exporterName, setExporterCommonConfig(gcpCloudMonitoringExporter, true, false));
        break;
      case LOKI:
        LokiConfig lokiConfig = (LokiConfig) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.LokiExporter lokiExporter =
            new OtelCollectorConfigFormat.LokiExporter();
        String endpoint = lokiConfig.getEndpoint();
        if (!endpoint.endsWith(TelemetryProviderService.LOKI_PUSH_ENDPOINT)) {
          endpoint = endpoint + TelemetryProviderService.LOKI_PUSH_ENDPOINT;
        }
        lokiExporter.setEndpoint(endpoint);
        Map<String, String> headers = new HashMap<>();
        boolean setHeaders = false;
        if (lokiConfig.getOrganizationID() != null && !lokiConfig.getOrganizationID().isEmpty()) {
          headers.put("X-Scope-OrgID", lokiConfig.getOrganizationID());
          setHeaders = true;
        }
        if (lokiConfig.getAuthType() == LokiConfig.LokiAuthType.BasicAuth) {
          String credentials =
              Base64.getEncoder()
                  .encodeToString(
                      (lokiConfig.getBasicAuth().getUsername()
                              + ":"
                              + lokiConfig.getBasicAuth().getPassword())
                          .getBytes());
          headers.put("Authorization", "Basic " + credentials);
          setHeaders = true;
        }
        if (setHeaders) {
          lokiExporter.setHeaders(headers);
        }
        exporterName = "loki/" + telemetryProvider.getUuid();
        exporters.put(exporterName, setExporterCommonConfig(lokiExporter, true, true));
        break;
      default:
        throw new IllegalArgumentException(
            "Exporter type "
                + telemetryProvider.getConfig().getType().name()
                + " is not supported.");
    }
    return exporterName;
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
      retryConfig.setMax_elapsed_time("1800m");
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

  private void appendSecretEnv(TelemetryProvider telemetryProvider, List<Object> secretEnv) {
    switch (telemetryProvider.getConfig().getType()) {
      case AWS_CLOUDWATCH:
        AWSCloudWatchConfig awsCloudWatchConfig =
            (AWSCloudWatchConfig) telemetryProvider.getConfig();
        if (StringUtils.isNotEmpty(awsCloudWatchConfig.getAccessKey())) {
          String encodedAccessKey =
              Base64.getEncoder().encodeToString(awsCloudWatchConfig.getAccessKey().getBytes());
          secretEnv.add(
              ImmutableMap.of("envName", "AWS_ACCESS_KEY_ID", "envValue", encodedAccessKey));
        }
        if (StringUtils.isNotEmpty(awsCloudWatchConfig.getSecretKey())) {
          String encodedSecretKey =
              Base64.getEncoder().encodeToString(awsCloudWatchConfig.getSecretKey().getBytes());
          secretEnv.add(
              ImmutableMap.of("envName", "AWS_SECRET_ACCESS_KEY", "envValue", encodedSecretKey));
        }
        break;
      case GCP_CLOUD_MONITORING:
        GCPCloudMonitoringConfig gcpCloudMonitoringConfig =
            (GCPCloudMonitoringConfig) telemetryProvider.getConfig();
        if (gcpCloudMonitoringConfig.getCredentials() != null) {
          String encodedCredentials =
              Base64.getEncoder()
                  .encodeToString(gcpCloudMonitoringConfig.getCredentials().toString().getBytes());
          secretEnv.add(
              ImmutableMap.of(
                  "envName",
                  "GOOGLE_APPLICATION_CREDENTIALS_CONTENT",
                  "envValue",
                  encodedCredentials));
        }
        break;
    }
  }
}
