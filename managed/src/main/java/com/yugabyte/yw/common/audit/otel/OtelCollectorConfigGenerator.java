package com.yugabyte.yw.common.audit.otel;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.audit.otel.OtelCollectorConfigFormat.MultilineConfig;
import com.yugabyte.yw.common.audit.otel.OtelCollectorConfigFormat.RetryConfig;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.yaml.SkipNullRepresenter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.AuthCredentials.AuthType;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import com.yugabyte.yw.models.helpers.telemetry.DynatraceConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import com.yugabyte.yw.models.helpers.telemetry.ExporterRetryConfig;
import com.yugabyte.yw.models.helpers.telemetry.GCPCloudMonitoringConfig;
import com.yugabyte.yw.models.helpers.telemetry.LokiConfig;
import com.yugabyte.yw.models.helpers.telemetry.OTLPConfig;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import com.yugabyte.yw.models.helpers.telemetry.S3Config;
import com.yugabyte.yw.models.helpers.telemetry.SplunkConfig;
import java.io.BufferedWriter;
import java.io.FileWriter;
import java.nio.file.Path;
import java.util.*;
import java.util.stream.Collectors;
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
  // Service names
  private static final String SERVICE_TSERVER = "yb-tserver";
  private static final String SERVICE_NODE_EXPORTER = "node-exporter";
  private static final String SERVICE_NODE_AGENT = "node-agent";
  private static final String SERVICE_OTEL_COLLECTOR = "otel-collector";
  private static final String SERVICE_OTEL_COLLECTOR_FULL = "yb-otel-collector";

  // Export types
  private static final String EXPORT_TYPE_MASTER = "master_export";
  private static final String EXPORT_TYPE_TSERVER = "tserver_export";
  private static final String EXPORT_TYPE_CQL = "cql_export";
  private static final String EXPORT_TYPE_YSQL = "ysql_export";
  private static final String EXPORT_TYPE_NODE = "node_export";
  private static final String EXPORT_TYPE_NODE_AGENT = "node_agent";
  private static final String EXPORT_TYPE_OTEL = "otel_export";

  // Log types
  private static final String LOG_TYPE_YSQL = "ysql";
  private static final String LOG_TYPE_YCQL = "ycql";

  // Job names
  private static final String JOB_NAME_YUGABYTE = "yugabyte";
  private static final String JOB_NAME_NODE = "node";
  private static final String JOB_NAME_NODE_AGENT = "node-agent";
  private static final String JOB_NAME_OTEL_COLLECTOR = "otel-collector";

  // Parameters
  private static final String PARAM_SHOW_HELP = "__param_show_help";
  private static final String PARAM_PRIORITY_REGEX = "__param_priority_regex";
  private static final String PARAM_METRICS = "__param_metrics";
  private static final String DEFAULT_SHOW_HELP = "false";

  // Receiver prefixes
  private static final String RECEIVER_PREFIX_FILELOG = "filelog/";
  private static final String RECEIVER_PREFIX_PROMETHEUS = "prometheus/";

  // Processor prefixes
  private static final String PROCESSOR_PREFIX_ATTRIBUTES = "attributes/";
  private static final String PROCESSOR_PREFIX_BATCH = "batch/";
  private static final String PROCESSOR_PREFIX_MEMORY_LIMITER = "memory_limiter/";
  private static final String PROCESSOR_PREFIX_TRANSFORM = "transform/";

  // Pipeline prefixes
  private static final String PIPELINE_PREFIX_LOGS = "logs/";
  private static final String PIPELINE_PREFIX_METRICS = "metrics/";

  // API endpoints
  private static final String DYNATRACE_OTLP_ENDPOINT = "/api/v2/otlp";

  // Processor prefixes
  private static final String PROCESSOR_PREFIX_CUMULATIVE_TO_DELTA = "cumulativetodelta/";
  private static final String PROCESSOR_PREFIX_METRIC_TRANSFORM = "metricstransform/";

  // Exporter prefixes
  private static final String EXPORTER_PREFIX_DATADOG = "datadog/";
  private static final String EXPORTER_PREFIX_SPLUNK = "splunk_hec/";
  private static final String EXPORTER_PREFIX_AWS_CLOUDWATCH = "awscloudwatchlogs/";
  private static final String EXPORTER_PREFIX_GCP_CLOUD_MONITORING = "googlecloud/";
  private static final String EXPORTER_PREFIX_LOKI = "loki/";
  private static final String EXPORTER_PREFIX_DYNATRACE = "otlphttp/";
  private static final String EXPORTER_PREFIX_S3 = "awss3/";

  // Export type prefixes
  private static final String EXPORT_TYPE_PREFIX_QUERY_LOGS = "query_logs_";
  private static final String EXPORT_TYPE_PREFIX_METRICS = "metrics_";

  // Common attribute strings
  private static final String ATTR_PREFIX_YUGABYTE = "yugabyte.";
  private static final String PURPOSE_SUFFIX_METRICS_EXPORT = "_METRICS_EXPORT";
  private static final String PURPOSE_SUFFIX_AUDIT_LOG_EXPORT = "_LOG_EXPORT";
  private static final String PURPOSE_SUFFIX_QUERY_LOG_EXPORT = "_QUERY_LOG_EXPORT";

  private final FileHelperService fileHelperService;
  private final TelemetryProviderService telemetryProviderService;
  private final RuntimeConfGetter confGetter;

  private final AuditLogRegexGenerator auditLogRegexGenerator;
  private final QueryLogRegexGenerator queryLogRegexGenerator;
  private final Environment environment;

  @Inject
  public OtelCollectorConfigGenerator(
      Environment environment,
      FileHelperService fileHelperService,
      TelemetryProviderService telemetryProviderService,
      AuditLogRegexGenerator auditLogRegexGenerator,
      QueryLogRegexGenerator queryLogRegexGenerator,
      RuntimeConfGetter confGetter) {
    this.environment = environment;
    this.fileHelperService = fileHelperService;
    this.telemetryProviderService = telemetryProviderService;
    this.auditLogRegexGenerator = auditLogRegexGenerator;
    this.queryLogRegexGenerator = queryLogRegexGenerator;
    this.confGetter = confGetter;
  }

  public Path generateConfigFile(
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      AuditLogConfig auditLogConfig,
      QueryLogConfig queryLogConfig,
      MetricsExportConfig metricsExportConfig,
      String logLinePrefix,
      int otelColMetricsPort,
      NodeAgent nodeAgent) {
    Path path =
        fileHelperService.createTempFile(
            "otel_collector_config_" + nodeParams.getUniverseUUID() + "_" + nodeParams.nodeUuid,
            ".yml");
    generateConfigFile(
        nodeParams,
        provider,
        userIntent,
        auditLogConfig,
        queryLogConfig,
        metricsExportConfig,
        logLinePrefix,
        path,
        otelColMetricsPort,
        nodeAgent);
    return path;
  }

  void generateConfigFile(
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      AuditLogConfig auditLogConfig,
      QueryLogConfig queryLogConfig,
      MetricsExportConfig metricsExportConfig,
      String logLinePrefix,
      Path path,
      int otelColMetricsPort,
      NodeAgent nodeAgent) {
    try (BufferedWriter writer = new BufferedWriter(new FileWriter(path.toFile()))) {
      Yaml yaml = new Yaml(new SkipNullRepresenter());
      OtelCollectorConfigFormat collectorConfigFormat = new OtelCollectorConfigFormat();
      addCommonService(collectorConfigFormat, provider, userIntent, otelColMetricsPort);
      if (auditLogConfig != null) {
        addAuditLogPipelines(
            collectorConfigFormat, nodeParams, provider, userIntent, auditLogConfig, logLinePrefix);
      }
      if (queryLogConfig != null) {
        addQueryLogPipelines(
            collectorConfigFormat, nodeParams, provider, userIntent, queryLogConfig, logLinePrefix);
      }
      if (metricsExportConfig != null) {
        addMetricsExporterPipelines(
            collectorConfigFormat,
            nodeParams,
            provider,
            userIntent,
            metricsExportConfig,
            logLinePrefix,
            otelColMetricsPort,
            nodeAgent);
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
          .put(
              RECEIVER_PREFIX_FILELOG + LOG_TYPE_YSQL, createYsqlReceiver(provider, logLinePrefix));
    }
    if (auditLogConfig != null
        && auditLogConfig.getYcqlAuditConfig() != null
        && auditLogConfig.getYcqlAuditConfig().isEnabled()) {
      collectorConfigFormat
          .getReceivers()
          .put(
              RECEIVER_PREFIX_FILELOG + LOG_TYPE_YCQL,
              createYcqlReceiver(provider, auditLogConfig.getYcqlAuditConfig()));
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

  private void addQueryLogPipelines(
      OtelCollectorConfigFormat collectorConfigFormat,
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      QueryLogConfig queryLogConfig,
      String logLinePrefix) {
    Customer customer = Customer.getOrBadRequest(provider.getCustomerUUID());
    Universe universe = Universe.getOrBadRequest(nodeParams.getUniverseUUID());

    if (OtelCollectorUtil.isQueryLogExportEnabledInUniverse(queryLogConfig)) {
      // Receivers
      if (queryLogConfig.getYsqlQueryLogConfig() != null
          && queryLogConfig.getYsqlQueryLogConfig().isEnabled()) {
        collectorConfigFormat
            .getReceivers()
            .put(
                RECEIVER_PREFIX_FILELOG + "query_logs_ysql",
                createYsqlQueryLogReceiver(provider, logLinePrefix));

        // Exporters
        List<String> currentProcessors =
            new ArrayList<>(collectorConfigFormat.getProcessors().keySet());
        queryLogConfig
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
  }

  private void addMetricsExporterPipelines(
      OtelCollectorConfigFormat collectorConfigFormat,
      NodeTaskParams nodeParams,
      Provider provider,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      MetricsExportConfig metricsExportConfig,
      String logLinePrefix,
      int otelColMetricsPort,
      NodeAgent nodeAgent) {
    if (metricsExportConfig != null
        && !CollectionUtils.isEmpty(metricsExportConfig.getUniverseMetricsExporterConfig())) {

      // Add a common Prometheus receiver for metrics export.
      Universe universe = Universe.getOrBadRequest(nodeParams.getUniverseUUID());
      NodeDetails nodeDetails = universe.getNode(nodeParams.getNodeName());
      collectorConfigFormat
          .getReceivers()
          .put(
              RECEIVER_PREFIX_PROMETHEUS + "yugabyte",
              createPrometheusReceiver(
                  metricsExportConfig,
                  nodeDetails.cloudInfo.private_ip,
                  universe,
                  otelColMetricsPort,
                  nodeDetails,
                  nodeAgent));

      // Add a new pipeline for each active metrics exporter config, since they can have different
      // additional tags.
      for (UniverseMetricsExporterConfig exporterConfig :
          metricsExportConfig.getUniverseMetricsExporterConfig()) {
        List<String> processorNames = new ArrayList<>();
        // Create a new pipeline for metrics export.
        OtelCollectorConfigFormat.Pipeline pipeline = new OtelCollectorConfigFormat.Pipeline();

        // Set the common Prometheus receiver for the pipeline
        pipeline.setReceivers(ImmutableList.of(RECEIVER_PREFIX_PROMETHEUS + "yugabyte"));

        TelemetryProvider telemetryProvider =
            telemetryProviderService.getOrBadRequest(exporterConfig.getExporterUuid());
        String exportTypeAndUUIDString =
            exportTypeAndUUID(telemetryProvider.getUuid(), ExportType.METRICS);

        // Add AttributesProcessor for metrics.
        String attributesProcessorName = PROCESSOR_PREFIX_ATTRIBUTES + exportTypeAndUUIDString;
        OtelCollectorConfigFormat.AttributesProcessor attributesProcessor =
            createMetricsExporterAttributesProcessor(
                exporterConfig,
                nodeParams.nodeName,
                nodeDetails,
                universe,
                exporterConfig.getAdditionalTags());
        if (attributesProcessor != null
            && !CollectionUtils.isEmpty(attributesProcessor.getActions())) {
          collectorConfigFormat.getProcessors().put(attributesProcessorName, attributesProcessor);
          // Add AttributesProcessor to the pipeline
          processorNames.add(attributesProcessorName);
        }

        // Add BatchProcessor for metrics.
        String batchProcessorName = PROCESSOR_PREFIX_BATCH + exportTypeAndUUIDString;
        OtelCollectorConfigFormat.BatchProcessor batchProcessor =
            new OtelCollectorConfigFormat.BatchProcessor();
        batchProcessor.setSend_batch_size(exporterConfig.getSendBatchSize());
        batchProcessor.setSend_batch_max_size(exporterConfig.getSendBatchMaxSize());
        batchProcessor.setTimeout(exporterConfig.getSendBatchTimeoutSeconds().toString() + "s");
        collectorConfigFormat.getProcessors().put(batchProcessorName, batchProcessor);
        // Add BatchProcessor to the pipeline
        processorNames.add(batchProcessorName);

        // Add MemoryLimiterProcessor for metrics.
        String memoryLimiterProcessorName =
            PROCESSOR_PREFIX_MEMORY_LIMITER + exportTypeAndUUIDString;
        OtelCollectorConfigFormat.MemoryLimiterProcessor memoryLimiterProcessor =
            new OtelCollectorConfigFormat.MemoryLimiterProcessor();
        memoryLimiterProcessor.setCheck_interval(
            exporterConfig.getMemoryLimitCheckIntervalSeconds().toString() + "s");
        memoryLimiterProcessor.setLimit_mib(exporterConfig.getMemoryLimitMib());
        collectorConfigFormat
            .getProcessors()
            .put(memoryLimiterProcessorName, memoryLimiterProcessor);
        // Add MemoryLimiterProcessor to the pipeline
        processorNames.add(memoryLimiterProcessorName);

        if (!StringUtils.isBlank(exporterConfig.getMetricsPrefix())) {
          String metricTransformProcessorName =
              PROCESSOR_PREFIX_METRIC_TRANSFORM + exportTypeAndUUIDString;
          OtelCollectorConfigFormat.MetricTransformProcessor metricTransformProcessor =
              new OtelCollectorConfigFormat.MetricTransformProcessor();

          // Create the transform rule to add custom prefix to all metrics
          OtelCollectorConfigFormat.MetricTransformRule transformRule =
              new OtelCollectorConfigFormat.MetricTransformRule();
          transformRule.setInclude("^(.*)$");
          transformRule.setMatch_type("regexp");
          transformRule.setAction("update");
          transformRule.setExperimental_match_labels(ImmutableMap.of("export_type", ".+"));
          transformRule.setNew_name(exporterConfig.getMetricsPrefix() + "$${1}");

          metricTransformProcessor.setTransforms(ImmutableList.of(transformRule));
          collectorConfigFormat
              .getProcessors()
              .put(metricTransformProcessorName, metricTransformProcessor);
          // Add MetricTransformProcessor to the pipeline
          processorNames.add(metricTransformProcessorName);
        }

        // Add CumulativeToDeltaProcessor for Dynatrace metrics only
        if (telemetryProvider.getConfig().getType() == ProviderType.DYNATRACE) {
          String cumulativetodeltaProcessorName =
              PROCESSOR_PREFIX_CUMULATIVE_TO_DELTA + exportTypeAndUUIDString;
          OtelCollectorConfigFormat.CumulativeToDeltaProcessor cumulativetodeltaProcessor =
              new OtelCollectorConfigFormat.CumulativeToDeltaProcessor();
          collectorConfigFormat
              .getProcessors()
              .put(cumulativetodeltaProcessorName, cumulativetodeltaProcessor);
          // Add CumulativeToDeltaProcessor to the pipeline
          processorNames.add(cumulativetodeltaProcessorName);
        }

        // Add all the processor names to the pipeline
        pipeline.setProcessors(processorNames);

        // Add metrics exporter for each active exporter config
        String exporterName = addMetricsExporter(collectorConfigFormat, exporterConfig);
        pipeline.setExporters(ImmutableList.of(exporterName));

        collectorConfigFormat
            .getService()
            .getPipelines()
            .put(PIPELINE_PREFIX_METRICS + exportTypeAndUUIDString, pipeline);
      }
    }
  }

  public Map<String, Object> getOtelHelmValues(
      AuditLogConfig auditLogConfig, String logLinePrefix) {
    // For K8s, we don't need to generate the entire config file.
    // We just need exporterCofigs and receiverConfigs.
    if (auditLogConfig == null) {
      return null;
    }
    Map<String, Object> otelConfig = new HashMap<>();

    if (!auditLogConfig.isExportActive()) {
      otelConfig.put("enabled", false);
    } else {

      otelConfig.put("enabled", true);
      // Recievers
      Map<String, Object> receivers = new HashMap<>();
      OtelCollectorConfigFormat.RegexOperator regexOperator =
          getRegexOperator(logLinePrefix, ExportType.AUDIT_LOGS);
      if (auditLogConfig.getYsqlAuditConfig() != null
          && auditLogConfig.getYsqlAuditConfig().isEnabled()) {
        Map<String, Object> ysqlReciever = new HashMap<>();
        ysqlReciever.put("regex", regexOperator.getRegex());
        ysqlReciever.put("timestamp", regexOperator.getTimestamp());
        ysqlReciever.put("lineStartPattern", generateLineStartPattern(logLinePrefix));
        receivers.put(LOG_TYPE_YSQL, ysqlReciever);
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
                          collectorConfigFormat,
                          new ArrayList<>(),
                          ExportType.AUDIT_LOGS);
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
    OtelCollectorConfigFormat.RegexOperator regexOperator =
        getRegexOperator(logPrefix, ExportType.AUDIT_LOGS);

    OtelCollectorConfigFormat.FileLogReceiver receiver =
        createFileLogReceiver(
            LOG_TYPE_YSQL, ImmutableList.of(filterOperator, regexOperator), ExportType.AUDIT_LOGS);
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

  private OtelCollectorConfigFormat.Receiver createYsqlQueryLogReceiver(
      Provider provider, String logPrefix) {
    OtelCollectorConfigFormat.FilterOperator filterOperator1 =
        new OtelCollectorConfigFormat.FilterOperator();
    // filtering out the audit logs
    filterOperator1.setType("filter");
    filterOperator1.setExpr("body matches \"^.*\\\\w+:  AUDIT:(.|\\\\n|\\\\r|\\\\s)*$\"");

    OtelCollectorConfigFormat.FilterOperator filterOperator2 =
        new OtelCollectorConfigFormat.FilterOperator();
    // Drop noisy PGSTAT logs
    filterOperator2.setType("filter");
    filterOperator2.setExpr(
        "body matches \"unable to find pgstat entry for abnormally terminated PID\"");

    OtelCollectorConfigFormat.FilterOperator filterOperator3 =
        new OtelCollectorConfigFormat.FilterOperator();
    // Filter out YBDB glob logs
    filterOperator3.setType("filter");
    filterOperator3.setExpr(
        "body matches \"^[IWEF]\\\\d{4} \\\\d{2}:\\\\d{2}:\\\\d{2}\\\\.\\\\d{6} \\\\d+"
            + " .*\\\\.cc:\\\\d+\"");

    OtelCollectorConfigFormat.RegexOperator regexOperator =
        getRegexOperator(logPrefix, ExportType.QUERY_LOGS);

    OtelCollectorConfigFormat.FileLogReceiver receiver =
        createFileLogReceiver(
            LOG_TYPE_YSQL,
            ImmutableList.of(filterOperator1, filterOperator2, filterOperator3, regexOperator),
            ExportType.QUERY_LOGS);
    receiver.setInclude(ImmutableList.of(provider.getYbHome() + "/tserver/logs/postgresql-*.log"));
    receiver.setExclude(ImmutableList.of(provider.getYbHome() + "/tserver/logs/*.gz"));

    MultilineConfig multilineConfig = new MultilineConfig();
    multilineConfig.setLine_start_pattern(generateQueryLineStartPattern(logPrefix));
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
        createFileLogReceiver(
            LOG_TYPE_YCQL, ImmutableList.of(filterOperator, regexOperator), ExportType.AUDIT_LOGS);
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
      String logType, List<OtelCollectorConfigFormat.Operator> operators, ExportType exportType) {
    OtelCollectorConfigFormat.FileLogReceiver receiver =
        new OtelCollectorConfigFormat.FileLogReceiver();
    receiver.setStart_at("beginning");
    receiver.setStorage("file_storage/queue");
    receiver.setOperators(operators);
    if (ExportType.AUDIT_LOGS.equals(exportType)) {
      receiver.setAttributes(ImmutableMap.of("yugabyte.audit_log_type", logType));
    } else if (ExportType.QUERY_LOGS.equals(exportType)) {
      receiver.setAttributes(ImmutableMap.of("yugabyte.query_log_type", logType));
    }
    return receiver;
  }

  private OtelCollectorConfigFormat.PrometheusReceiver createPrometheusReceiver(
      MetricsExportConfig metricsExportConfig,
      String nodeAddress,
      Universe universe,
      int otelColMetricsPort,
      NodeDetails nodeDetails,
      NodeAgent nodeAgent) {
    OtelCollectorConfigFormat.PrometheusReceiver receiver =
        new OtelCollectorConfigFormat.PrometheusReceiver();
    OtelCollectorConfigFormat.PrometheusConfig config =
        new OtelCollectorConfigFormat.PrometheusConfig();

    // Set global configuration
    OtelCollectorConfigFormat.GlobalConfig globalConfig =
        new OtelCollectorConfigFormat.GlobalConfig();
    globalConfig.setScrape_interval(metricsExportConfig.getScrapeIntervalSeconds() + "s");
    globalConfig.setScrape_timeout(metricsExportConfig.getScrapeTimeoutSeconds() + "s");
    config.setGlobal(globalConfig);

    List<OtelCollectorConfigFormat.ScrapeConfig> scrapeConfigs = new ArrayList<>();

    // Add yugabyte scrape config. Includes master, tserver, ysql, cql exports.
    if (OtelCollectorUtil.yugabyteJobScrapeConfigEnabled(
        metricsExportConfig.getScrapeConfigTargets())) {
      scrapeConfigs.add(createYugabyteScrapeConfig(nodeAddress, nodeDetails, metricsExportConfig));
    }

    // Add node exporter scrape config if the scrape config target is enabled.
    if (metricsExportConfig.getScrapeConfigTargets().contains(ScrapeConfigTargetType.NODE_EXPORT)) {
      scrapeConfigs.add(createNodeExporterScrapeConfig(nodeAddress, nodeDetails));
    }

    // Add node-agent scrape config if the scrape config target is enabled.
    if (metricsExportConfig
        .getScrapeConfigTargets()
        .contains(ScrapeConfigTargetType.NODE_AGENT_EXPORT)) {
      scrapeConfigs.add(createNodeAgentScrapeConfig(nodeAddress, nodeAgent));
    }

    // Add otel-collector scrape config if the scrape config target is enabled.
    if (metricsExportConfig.getScrapeConfigTargets().contains(ScrapeConfigTargetType.OTEL_EXPORT)) {
      scrapeConfigs.add(createOtelCollectorScrapeConfig(nodeAddress, universe, otelColMetricsPort));
    }

    // Set the scrape configs to the prometheus receiver
    config.setScrape_configs(scrapeConfigs);
    receiver.setConfig(config);
    return receiver;
  }

  public String getPriorityRegex(MetricCollectionLevel level) {
    return level.getPriorityRegex(environment);
  }

  public String getMetrics(MetricCollectionLevel level) {
    return level.getMetrics(environment);
  }

  public OtelCollectorConfigFormat.StaticConfig createMasterStaticConfig(
      String nodeAddress, NodeDetails nodeDetails, MetricsExportConfig metricsExportConfig) {
    OtelCollectorConfigFormat.StaticConfig staticConfig =
        new OtelCollectorConfigFormat.StaticConfig();
    // Get the master HTTP port from node details
    int masterPort = nodeDetails.masterHttpPort;
    staticConfig.setTargets(ImmutableList.of(nodeAddress + ":" + masterPort));

    // Build up all labels
    Map<String, String> labels = new HashMap<>();
    labels.put("export_type", EXPORT_TYPE_MASTER);
    labels.put("service", "yb-master");

    // Get the metric collection level from the metrics export config
    MetricCollectionLevel level = metricsExportConfig.getCollectionLevel();
    String priorityRegex = getPriorityRegex(level);
    if (!StringUtils.isEmpty(priorityRegex)) {
      labels.put(PARAM_PRIORITY_REGEX, priorityRegex);
    }

    // For minimal collection level, also add the 'metrics' param to limit server level metrics
    String metrics = getMetrics(level);
    if (!StringUtils.isEmpty(metrics)) {
      labels.put(PARAM_METRICS, metrics);
    }

    labels.put(PARAM_SHOW_HELP, DEFAULT_SHOW_HELP);

    // Set all labels at once
    staticConfig.setLabels(ImmutableMap.copyOf(labels));
    return staticConfig;
  }

  private OtelCollectorConfigFormat.StaticConfig createTserverStaticConfig(
      String nodeAddress, NodeDetails nodeDetails, MetricsExportConfig metricsExportConfig) {
    OtelCollectorConfigFormat.StaticConfig staticConfig =
        new OtelCollectorConfigFormat.StaticConfig();
    // Get the tserver HTTP port from node details
    int tserverPort = nodeDetails.tserverHttpPort;
    staticConfig.setTargets(ImmutableList.of(nodeAddress + ":" + tserverPort));

    // Build up all labels
    Map<String, String> labels = new HashMap<>();
    labels.put("export_type", EXPORT_TYPE_TSERVER);
    labels.put("service", SERVICE_TSERVER);

    // Get the metric collection level from the metrics export config
    MetricCollectionLevel level = metricsExportConfig.getCollectionLevel();
    String priorityRegex = getPriorityRegex(level);
    if (!StringUtils.isEmpty(priorityRegex)) {
      labels.put(PARAM_PRIORITY_REGEX, priorityRegex);
    }

    // For minimal collection level, also add the 'metrics' param to limit server level metrics
    String metrics = getMetrics(level);
    if (!StringUtils.isEmpty(metrics)) {
      labels.put(PARAM_METRICS, metrics);
    }

    labels.put(PARAM_SHOW_HELP, DEFAULT_SHOW_HELP);

    // Set all labels at once
    staticConfig.setLabels(ImmutableMap.copyOf(labels));
    return staticConfig;
  }

  private OtelCollectorConfigFormat.StaticConfig createYsqlStaticConfig(
      String nodeAddress, NodeDetails nodeDetails) {
    OtelCollectorConfigFormat.StaticConfig staticConfig =
        new OtelCollectorConfigFormat.StaticConfig();
    // Get the YSQL server HTTP port from node details
    int ysqlPort = nodeDetails.ysqlServerHttpPort;
    staticConfig.setTargets(ImmutableList.of(nodeAddress + ":" + ysqlPort));

    // Build up all labels
    Map<String, String> labels = new HashMap<>();
    labels.put("export_type", EXPORT_TYPE_YSQL);
    labels.put("service", SERVICE_TSERVER);
    labels.put(PARAM_SHOW_HELP, DEFAULT_SHOW_HELP);

    // Set all labels at once
    staticConfig.setLabels(ImmutableMap.copyOf(labels));
    return staticConfig;
  }

  private OtelCollectorConfigFormat.StaticConfig createCqlStaticConfig(
      String nodeAddress, NodeDetails nodeDetails) {
    OtelCollectorConfigFormat.StaticConfig staticConfig =
        new OtelCollectorConfigFormat.StaticConfig();
    // Get the YCQL server HTTP port from node details
    int yqlPort = nodeDetails.yqlServerHttpPort;
    staticConfig.setTargets(ImmutableList.of(nodeAddress + ":" + yqlPort));

    // Build up all labels
    Map<String, String> labels = new HashMap<>();
    labels.put("export_type", EXPORT_TYPE_CQL);
    labels.put("service", SERVICE_TSERVER);
    labels.put(PARAM_SHOW_HELP, DEFAULT_SHOW_HELP);

    // Set all labels at once
    staticConfig.setLabels(ImmutableMap.copyOf(labels));
    return staticConfig;
  }

  private OtelCollectorConfigFormat.MetricRelabelConfig createMetricRelabelConfig(
      List<String> sourceLabels, String regex, String targetLabel, String replacement) {
    OtelCollectorConfigFormat.MetricRelabelConfig config =
        new OtelCollectorConfigFormat.MetricRelabelConfig();
    config.setSource_labels(sourceLabels);
    config.setRegex(regex);
    config.setTarget_label(targetLabel);
    config.setReplacement(replacement);
    return config;
  }

  private List<OtelCollectorConfigFormat.MetricRelabelConfig> createYugabyteMetricRelabelConfigs() {
    List<OtelCollectorConfigFormat.MetricRelabelConfig> configs = new ArrayList<>();

    // Add yugabyte metric relabel configs
    configs.add(
        createMetricRelabelConfig(ImmutableList.of("__name__"), "(.*)", "saved_name", "$1"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(.*)",
            "server_type",
            "$1"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(.*)",
            "service_type",
            "$2"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(_sum|_count)?",
            "service_method",
            "$3"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(_sum|_count)?",
            "__name__",
            "rpc_latency$4"));

    return configs;
  }

  private OtelCollectorConfigFormat.ScrapeConfig createYugabyteScrapeConfig(
      String nodeAddress, NodeDetails nodeDetails, MetricsExportConfig metricsExportConfig) {
    OtelCollectorConfigFormat.ScrapeConfig scrapeConfig =
        new OtelCollectorConfigFormat.ScrapeConfig();
    scrapeConfig.setJob_name(JOB_NAME_YUGABYTE);
    scrapeConfig.setScheme("http");

    // Add TLS config
    OtelCollectorConfigFormat.TlsConfig tlsConfig = new OtelCollectorConfigFormat.TlsConfig();
    tlsConfig.setInsecure_skip_verify(true);
    scrapeConfig.setTls_config(tlsConfig);

    scrapeConfig.setMetrics_path("/prometheus-metrics");

    // Create static configs for all yugabyte services
    List<OtelCollectorConfigFormat.StaticConfig> staticConfigs = new ArrayList<>();
    // Add static configs for each yugabyte service if the scrape config target is enabled.
    if (metricsExportConfig
        .getScrapeConfigTargets()
        .contains(ScrapeConfigTargetType.MASTER_EXPORT)) {
      staticConfigs.add(createMasterStaticConfig(nodeAddress, nodeDetails, metricsExportConfig));
    }
    if (metricsExportConfig
        .getScrapeConfigTargets()
        .contains(ScrapeConfigTargetType.TSERVER_EXPORT)) {
      staticConfigs.add(createTserverStaticConfig(nodeAddress, nodeDetails, metricsExportConfig));
    }
    if (metricsExportConfig.getScrapeConfigTargets().contains(ScrapeConfigTargetType.YSQL_EXPORT)) {
      staticConfigs.add(createYsqlStaticConfig(nodeAddress, nodeDetails));
    }
    if (metricsExportConfig.getScrapeConfigTargets().contains(ScrapeConfigTargetType.CQL_EXPORT)) {
      staticConfigs.add(createCqlStaticConfig(nodeAddress, nodeDetails));
    }

    scrapeConfig.setStatic_configs(staticConfigs);
    scrapeConfig.setMetric_relabel_configs(createYugabyteMetricRelabelConfigs());

    return scrapeConfig;
  }

  private List<OtelCollectorConfigFormat.MetricRelabelConfig> createNodeMetricRelabelConfigs() {
    List<OtelCollectorConfigFormat.MetricRelabelConfig> configs = new ArrayList<>();

    // Add all the node metric relabel configs
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"), "node_cpu", "__name__", "node_cpu_seconds_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_filesystem_free",
            "__name__",
            "node_filesystem_free_bytes"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_filesystem_size",
            "__name__",
            "node_filesystem_size_bytes"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_disk_reads_completed",
            "__name__",
            "node_disk_reads_completed_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_disk_writes_completed",
            "__name__",
            "node_disk_writes_completed_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_memory_MemTotal",
            "__name__",
            "node_memory_MemTotal_bytes"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_memory_Slab",
            "__name__",
            "node_memory_Slab_bytes"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_memory_Cached",
            "__name__",
            "node_memory_Cached_bytes"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_memory_Buffers",
            "__name__",
            "node_memory_Buffers_bytes"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_memory_MemFree",
            "__name__",
            "node_memory_MemFree_bytes"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_network_receive_bytes",
            "__name__",
            "node_network_receive_bytes_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_network_transmit_bytes",
            "__name__",
            "node_network_transmit_bytes_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_network_receive_packets",
            "__name__",
            "node_network_receive_packets_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_network_transmit_packets",
            "__name__",
            "node_network_transmit_packets_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_network_receive_errs",
            "__name__",
            "node_network_receive_errs_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_network_transmit_errs",
            "__name__",
            "node_network_transmit_errs_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_disk_bytes_read",
            "__name__",
            "node_disk_read_bytes_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"),
            "node_disk_bytes_written",
            "__name__",
            "node_disk_written_bytes_total"));
    configs.add(
        createMetricRelabelConfig(
            ImmutableList.of("__name__"), "node_boot_time", "__name__", "node_boot_time_seconds"));
    configs.add(
        createMetricRelabelConfig(ImmutableList.of("__name__"), "(.*)", "saved_name", "$1"));

    return configs;
  }

  private OtelCollectorConfigFormat.ScrapeConfig createNodeExporterScrapeConfig(
      String nodeAddress, NodeDetails nodeDetails) {
    OtelCollectorConfigFormat.ScrapeConfig scrapeConfig =
        new OtelCollectorConfigFormat.ScrapeConfig();
    scrapeConfig.setJob_name(JOB_NAME_NODE);
    scrapeConfig.setScheme("http");
    scrapeConfig.setMetrics_path("/metrics");

    // Add TLS config
    OtelCollectorConfigFormat.TlsConfig tlsConfig = new OtelCollectorConfigFormat.TlsConfig();
    tlsConfig.setInsecure_skip_verify(true);
    scrapeConfig.setTls_config(tlsConfig);

    OtelCollectorConfigFormat.StaticConfig staticConfig =
        new OtelCollectorConfigFormat.StaticConfig();

    // Get the node exporter port from node details
    int nodeExporterPort = nodeDetails.nodeExporterPort;
    staticConfig.setTargets(ImmutableList.of(nodeAddress + ":" + nodeExporterPort));

    // Build up all labels
    Map<String, String> labels = new HashMap<>();
    labels.put("export_type", EXPORT_TYPE_NODE);
    labels.put("service", SERVICE_NODE_EXPORTER);

    // Set all labels at once
    staticConfig.setLabels(ImmutableMap.copyOf(labels));

    scrapeConfig.setStatic_configs(ImmutableList.of(staticConfig));
    scrapeConfig.setMetric_relabel_configs(createNodeMetricRelabelConfigs());

    return scrapeConfig;
  }

  private OtelCollectorConfigFormat.ScrapeConfig createNodeAgentScrapeConfig(
      String nodeAddress, NodeAgent nodeAgent) {
    OtelCollectorConfigFormat.ScrapeConfig scrapeConfig =
        new OtelCollectorConfigFormat.ScrapeConfig();
    scrapeConfig.setJob_name(JOB_NAME_NODE_AGENT);
    scrapeConfig.setScheme("http");
    scrapeConfig.setMetrics_path("/metrics");

    // Add TLS config
    OtelCollectorConfigFormat.TlsConfig tlsConfig = new OtelCollectorConfigFormat.TlsConfig();
    tlsConfig.setInsecure_skip_verify(true);
    scrapeConfig.setTls_config(tlsConfig);

    OtelCollectorConfigFormat.StaticConfig staticConfig =
        new OtelCollectorConfigFormat.StaticConfig();

    // Get the node agent server port from runtime config, default to 9070 if not specified
    int nodeAgentPort = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentServerPort);
    staticConfig.setTargets(ImmutableList.of(nodeAddress + ":" + nodeAgentPort));

    // Build up all labels
    Map<String, String> labels = new HashMap<>();
    labels.put("export_type", EXPORT_TYPE_NODE_AGENT);
    labels.put("service", SERVICE_NODE_AGENT);
    if (nodeAgent != null) {
      labels.put("node_agent_uuid", nodeAgent.getUuid().toString());
    }

    // Set all labels at once
    staticConfig.setLabels(ImmutableMap.copyOf(labels));

    scrapeConfig.setStatic_configs(ImmutableList.of(staticConfig));
    return scrapeConfig;
  }

  private OtelCollectorConfigFormat.ScrapeConfig createOtelCollectorScrapeConfig(
      String nodeAddress, Universe universe, int otelColMetricsPort) {
    OtelCollectorConfigFormat.ScrapeConfig scrapeConfig =
        new OtelCollectorConfigFormat.ScrapeConfig();
    scrapeConfig.setJob_name(JOB_NAME_OTEL_COLLECTOR);
    scrapeConfig.setScheme("http");
    scrapeConfig.setMetrics_path("/metrics");

    // Add TLS config
    OtelCollectorConfigFormat.TlsConfig tlsConfig = new OtelCollectorConfigFormat.TlsConfig();
    tlsConfig.setInsecure_skip_verify(true);
    scrapeConfig.setTls_config(tlsConfig);

    OtelCollectorConfigFormat.StaticConfig staticConfig =
        new OtelCollectorConfigFormat.StaticConfig();

    staticConfig.setTargets(ImmutableList.of(nodeAddress + ":" + otelColMetricsPort));

    // Build up all labels
    Map<String, String> labels = new HashMap<>();
    labels.put("export_type", EXPORT_TYPE_OTEL);
    labels.put("service", SERVICE_OTEL_COLLECTOR);

    // Set all labels at once
    staticConfig.setLabels(ImmutableMap.copyOf(labels));

    scrapeConfig.setStatic_configs(ImmutableList.of(staticConfig));
    return scrapeConfig;
  }

  private OtelCollectorConfigFormat.AttributesProcessor createMetricsExporterAttributesProcessor(
      UniverseMetricsExporterConfig exporterConfig,
      String nodeName,
      NodeDetails nodeDetails,
      Universe universe,
      Map<String, String> additionalTags) {
    TelemetryProvider telemetryProvider =
        telemetryProviderService.getOrBadRequest(exporterConfig.getExporterUuid());

    List<OtelCollectorConfigFormat.AttributeAction> attributeActions = new ArrayList<>();
    // Add common required attributes
    addCommonRequiredAttributes(
        attributeActions,
        nodeName,
        nodeDetails,
        universe,
        telemetryProvider,
        PURPOSE_SUFFIX_METRICS_EXPORT);

    // Add common additional attributes from the exporter config and additional tags from the log
    // config payload.
    addCommonAdditionalAttributes(attributeActions, telemetryProvider, additionalTags);

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
            telemetryProvider, collectorConfigFormat, new ArrayList<>(), ExportType.METRICS);

    return exporterName;
  }

  private OtelCollectorConfigFormat.RegexOperator getRegexOperator(
      String logPrefix, ExportType exportType) {
    AuditLogRegexGenerator.LogRegexResult regexResult;
    switch (exportType) {
      case AUDIT_LOGS:
        regexResult = auditLogRegexGenerator.generateAuditLogRegex(logPrefix, /*onlyPrefix*/ false);
        break;
      case QUERY_LOGS:
        regexResult = queryLogRegexGenerator.generateQueryLogRegex(logPrefix, /*onlyPrefix*/ false);
        break;
      default:
        throw new IllegalArgumentException("Unsupported export type: " + exportType);
    }
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

  private String generateQueryLineStartPattern(String logPrefix) {
    return ".*([A-Z]\\d{4})|("
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
    String exporterName;
    List<OtelCollectorConfigFormat.AttributeAction> attributeActions = new ArrayList<>();
    OtelCollectorConfigFormat.AttributesProcessor attributesProcessor =
        new OtelCollectorConfigFormat.AttributesProcessor();

    exporterName =
        appendExporterConfig(
            telemetryProvider,
            collectorConfig,
            attributeActions,
            ExportType.AUDIT_LOGS,
            universe.getUniverseUUID(),
            nodeName);

    // Add common log attributes, log prefix extraction, and tags
    AuditLogRegexGenerator.LogRegexResult regexResult =
        auditLogRegexGenerator.generateAuditLogRegex(logLinePrefix, /*onlyPrefix*/ true);
    addCommonLogAttributes(
        attributeActions,
        nodeName,
        nodeDetails,
        universe,
        telemetryProvider,
        PURPOSE_SUFFIX_AUDIT_LOG_EXPORT,
        true,
        regexResult,
        logsExporterConfig.getAdditionalTags());

    attributesProcessor.setActions(attributeActions);

    String exporterTypeAndUUIDString =
        exportTypeAndUUID(telemetryProvider.getUuid(), ExportType.AUDIT_LOGS);
    String processorName = PROCESSOR_PREFIX_ATTRIBUTES + exporterTypeAndUUIDString;
    collectorConfig.getProcessors().put(processorName, attributesProcessor);
    List<String> processorNames = new ArrayList<>(currentProcessors);
    processorNames.add(processorName);

    // Add common transform processor
    addCommonTransformProcessor(collectorConfig);

    OtelCollectorConfigFormat.Pipeline pipeline = new OtelCollectorConfigFormat.Pipeline();
    List<String> receivers = new ArrayList<>(collectorConfig.getReceivers().keySet());
    List<String> auditReceivers =
        receivers.stream()
            .filter(receiver -> !receiver.contains("query_logs"))
            .collect(Collectors.toList());
    pipeline.setReceivers(auditReceivers);

    List<String> processors = new ArrayList<>(collectorConfig.getProcessors().keySet());
    // Filter processors to only include those with audit_log in their name
    List<String> auditProcessors =
        processors.stream()
            .filter(
                processor ->
                    processor.contains(exporterTypeAndUUIDString)
                        || processor.contains(PROCESSOR_PREFIX_TRANSFORM))
            .collect(Collectors.toList());

    pipeline.setProcessors(auditProcessors);
    pipeline.setExporters(ImmutableList.of(exporterName));
    // Use unique pipeline key for audit logs
    collectorConfig
        .getService()
        .getPipelines()
        .put(PIPELINE_PREFIX_LOGS + exporterTypeAndUUIDString, pipeline);
  }

  private void appendExporter(
      Customer customer,
      Universe universe,
      OtelCollectorConfigFormat collectorConfig,
      UniverseQueryLogsExporterConfig logsExporterConfig,
      List<String> currentProcessors,
      String nodeName,
      String logLinePrefix) {
    NodeDetails nodeDetails = universe.getNode(nodeName);
    TelemetryProvider telemetryProvider =
        telemetryProviderService.getOrBadRequest(logsExporterConfig.getExporterUuid());
    String exporterName;
    List<OtelCollectorConfigFormat.AttributeAction> attributeActions = new ArrayList<>();
    OtelCollectorConfigFormat.AttributesProcessor attributesProcessor =
        new OtelCollectorConfigFormat.AttributesProcessor();

    exporterName =
        appendExporterConfig(
            telemetryProvider,
            collectorConfig,
            attributeActions,
            ExportType.QUERY_LOGS,
            universe.getUniverseUUID(),
            nodeName);

    // Add common log attributes, log prefix extraction, and tags
    AuditLogRegexGenerator.LogRegexResult regexResult =
        queryLogRegexGenerator.generateQueryLogRegex(logLinePrefix, /*onlyPrefix*/ true);
    addCommonLogAttributes(
        attributeActions,
        nodeName,
        nodeDetails,
        universe,
        telemetryProvider,
        PURPOSE_SUFFIX_QUERY_LOG_EXPORT,
        false,
        regexResult,
        logsExporterConfig.getAdditionalTags());

    attributesProcessor.setActions(attributeActions);

    String exportTypeAndUUIDString =
        exportTypeAndUUID(telemetryProvider.getUuid(), ExportType.QUERY_LOGS);
    String processorName = PROCESSOR_PREFIX_ATTRIBUTES + exportTypeAndUUIDString;
    collectorConfig.getProcessors().put(processorName, attributesProcessor);
    List<String> processorNames = new ArrayList<>(currentProcessors);
    processorNames.add(processorName);

    OtelCollectorConfigFormat.BatchProcessor batchProcessor =
        new OtelCollectorConfigFormat.BatchProcessor();
    batchProcessor.setSend_batch_max_size(logsExporterConfig.getSendBatchMaxSize());
    batchProcessor.setSend_batch_size(logsExporterConfig.getSendBatchSize());
    batchProcessor.setTimeout(logsExporterConfig.getSendBatchTimeoutSeconds().toString() + "s");
    String batchProcessorName = PROCESSOR_PREFIX_BATCH + exportTypeAndUUIDString;
    collectorConfig.getProcessors().put(batchProcessorName, batchProcessor);
    processorNames.add(batchProcessorName);

    String memoryLimiterProcessorName = PROCESSOR_PREFIX_MEMORY_LIMITER + exportTypeAndUUIDString;
    OtelCollectorConfigFormat.MemoryLimiterProcessor memoryLimiterProcessor =
        new OtelCollectorConfigFormat.MemoryLimiterProcessor();
    memoryLimiterProcessor.setCheck_interval(
        logsExporterConfig.getMemoryLimitCheckIntervalSeconds().toString() + "s");
    memoryLimiterProcessor.setLimit_mib(logsExporterConfig.getMemoryLimitMib());
    collectorConfig.getProcessors().put(memoryLimiterProcessorName, memoryLimiterProcessor);
    processorNames.add(memoryLimiterProcessorName);

    // Add common transform processor
    addCommonTransformProcessor(collectorConfig);

    OtelCollectorConfigFormat.Pipeline pipeline = new OtelCollectorConfigFormat.Pipeline();
    List<String> receivers = new ArrayList<>(collectorConfig.getReceivers().keySet());
    List<String> queryReceivers =
        receivers.stream()
            .filter(receiver -> receiver.contains("query_logs"))
            .collect(Collectors.toList());
    pipeline.setReceivers(queryReceivers);

    List<String> processors = new ArrayList<>(collectorConfig.getProcessors().keySet());
    // Filter processors to only include those with query_log, plus the common transform processor
    List<String> queryProcessors =
        processors.stream()
            .filter(
                processor ->
                    processor.contains(exportTypeAndUUIDString)
                        || processor.contains(PROCESSOR_PREFIX_TRANSFORM))
            .collect(Collectors.toList());

    pipeline.setProcessors(queryProcessors);
    pipeline.setExporters(ImmutableList.of(exporterName));
    // Use unique pipeline key for query logs
    collectorConfig
        .getService()
        .getPipelines()
        .put(PIPELINE_PREFIX_LOGS + exportTypeAndUUIDString, pipeline);
  }

  private List<OtelCollectorConfigFormat.AttributeAction> getTagsToAttributeActions(
      Map<String, String> tags) {
    return tags.entrySet().stream()
        .map(
            e ->
                new OtelCollectorConfigFormat.AttributeAction(
                    e.getKey(), e.getValue(), "upsert", null))
        .toList();
  }

  private void addCommonRequiredAttributes(
      List<OtelCollectorConfigFormat.AttributeAction> attributeActions,
      String nodeName,
      NodeDetails nodeDetails,
      Universe universe,
      TelemetryProvider telemetryProvider,
      String purposeSuffix) {
    // Add some common collector labels.
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction("host", nodeName, "upsert", null));
    attributeActions.add(
        new OtelCollectorConfigFormat.AttributeAction(
            "yugabyte.node_name", nodeName, "upsert", null));
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
            telemetryProvider.getConfig().getType().toString() + purposeSuffix,
            "upsert",
            null));
  }

  private void addCommonAdditionalAttributes(
      List<OtelCollectorConfigFormat.AttributeAction> attributeActions,
      TelemetryProvider telemetryProvider,
      Map<String, String> additionalTags) {
    // Override or add tags from the exporter config.
    if (MapUtils.isNotEmpty(telemetryProvider.getTags())) {
      attributeActions.addAll(getTagsToAttributeActions(telemetryProvider.getTags()));
    }

    // Override or add additional tags from the log config payload.
    if (MapUtils.isNotEmpty(additionalTags)) {
      attributeActions.addAll(getTagsToAttributeActions(additionalTags));
    }
  }

  private void addCommonLogAttributes(
      List<OtelCollectorConfigFormat.AttributeAction> attributeActions,
      String nodeName,
      NodeDetails nodeDetails,
      Universe universe,
      TelemetryProvider telemetryProvider,
      String purposeSuffix,
      boolean includeAuditType,
      AuditLogRegexGenerator.LogRegexResult regexResult,
      Map<String, String> additionalTags) {
    // Add some common collector labels.
    addCommonRequiredAttributes(
        attributeActions, nodeName, nodeDetails, universe, telemetryProvider, purposeSuffix);

    // Rename the common attributes to organise under the key yugabyte.
    List<RenamePair> commonRenamePairs = new ArrayList<RenamePair>();
    commonRenamePairs.add(new RenamePair("log.file.name", ATTR_PREFIX_YUGABYTE + "log.file.name"));
    commonRenamePairs.add(new RenamePair("log_level", ATTR_PREFIX_YUGABYTE + "log_level"));
    if (includeAuditType) {
      commonRenamePairs.add(new RenamePair("audit_type", ATTR_PREFIX_YUGABYTE + "audit_type"));
    }
    commonRenamePairs.add(new RenamePair("statement_id", ATTR_PREFIX_YUGABYTE + "statement_id"));
    commonRenamePairs.add(
        new RenamePair("substatement_id", ATTR_PREFIX_YUGABYTE + "substatement_id"));
    commonRenamePairs.add(new RenamePair("class", ATTR_PREFIX_YUGABYTE + "class"));
    commonRenamePairs.add(new RenamePair("command", ATTR_PREFIX_YUGABYTE + "command"));
    commonRenamePairs.add(new RenamePair("object_type", ATTR_PREFIX_YUGABYTE + "object_type"));
    commonRenamePairs.add(new RenamePair("object_name", ATTR_PREFIX_YUGABYTE + "object_name"));
    commonRenamePairs.add(new RenamePair("statement", ATTR_PREFIX_YUGABYTE + "statement"));
    commonRenamePairs.forEach(rp -> attributeActions.addAll(rp.getRenameAttributeActions()));

    // Rename the log prefix extracted attributes to come under the key yugabyte.
    regexResult
        .getTokens()
        .forEach(
            token -> {
              RenamePair rp =
                  new RenamePair(token.getAttributeName(), token.getYugabyteAttributeName());
              attributeActions.addAll(rp.getRenameAttributeActions());
            });

    // Override or add tags from the exporter config and additional tags from the log config
    // payload.
    addCommonAdditionalAttributes(attributeActions, telemetryProvider, additionalTags);
  }

  private String appendExporterConfig(
      TelemetryProvider telemetryProvider,
      OtelCollectorConfigFormat collectorConfig,
      List<OtelCollectorConfigFormat.AttributeAction> attributeActions,
      ExportType exportType) {
    return appendExporterConfig(
        telemetryProvider, collectorConfig, attributeActions, exportType, null, null);
  }

  private String appendExporterConfig(
      TelemetryProvider telemetryProvider,
      OtelCollectorConfigFormat collectorConfig,
      List<OtelCollectorConfigFormat.AttributeAction> attributeActions,
      ExportType exportType,
      UUID universeUUID,
      String nodeName) {
    String exporterName;
    String exportTypeAndUUIDString = exportTypeAndUUID(telemetryProvider.getUuid(), exportType);
    Map<String, OtelCollectorConfigFormat.Exporter> exporters = collectorConfig.getExporters();
    Map<String, OtelCollectorConfigFormat.Extension> extensions = collectorConfig.getExtensions();
    boolean setExtension = false;
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
        exporterName = EXPORTER_PREFIX_DATADOG + exportTypeAndUUIDString;
        exporters.put(
            exporterName, setExporterCommonConfig(dataDogExporter, true, true, exportType));

        // Add Datadog specific labels.
        attributeActions.add(
            new OtelCollectorConfigFormat.AttributeAction("ddsource", "yugabyte", "upsert", null));
        attributeActions.add(
            new OtelCollectorConfigFormat.AttributeAction(
                "service", SERVICE_OTEL_COLLECTOR_FULL, "upsert", null));
        break;
      case DYNATRACE:
        DynatraceConfig dynatraceConfig = (DynatraceConfig) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.DynatraceExporter dynatraceExporter =
            new OtelCollectorConfigFormat.DynatraceExporter();
        // Construct the full endpoint with the OTLP path
        String fullEndpoint = dynatraceConfig.getCleanEndpoint() + DYNATRACE_OTLP_ENDPOINT;
        dynatraceExporter.setEndpoint(fullEndpoint);

        // Set the Authorization header with the API token
        Map<String, String> dynatraceHeaders = new HashMap<>();
        dynatraceHeaders.put("Authorization", "Api-Token " + dynatraceConfig.getApiToken());
        dynatraceExporter.setHeaders(dynatraceHeaders);

        exporterName = EXPORTER_PREFIX_DYNATRACE + exportTypeAndUUIDString;
        exporters.put(
            exporterName, setExporterCommonConfig(dynatraceExporter, true, true, exportType));
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
        exporterName = EXPORTER_PREFIX_SPLUNK + exportTypeAndUUIDString;
        OtelCollectorConfigFormat.TlsSettings tlsSettings =
            new OtelCollectorConfigFormat.TlsSettings();
        tlsSettings.setInsecure_skip_verify(true);
        splunkExporter.setTls(tlsSettings);

        exporters.put(
            exporterName, setExporterCommonConfig(splunkExporter, true, true, exportType));

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

        exporterName = EXPORTER_PREFIX_AWS_CLOUDWATCH + exportTypeAndUUIDString;
        exporters.put(
            exporterName, setExporterCommonConfig(awsCloudWatchExporter, false, true, exportType));

        break;
      case S3:
        S3Config s3Config = (S3Config) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.AWSS3Exporter s3Exporter =
            new OtelCollectorConfigFormat.AWSS3Exporter();
        s3Exporter.setMarshaler(s3Config.getMarshaler().getName());
        OtelCollectorConfigFormat.S3UploaderConfig s3UploaderConfig =
            new OtelCollectorConfigFormat.S3UploaderConfig();
        s3UploaderConfig.setS3_bucket(s3Config.getBucket());

        // Build S3 prefix - include universe UUID and node name if enabled
        String s3Prefix = s3Config.getDirectoryPrefix();
        if (s3Config.getIncludeUniverseAndNodeInPrefix()
            && universeUUID != null
            && nodeName != null) {
          // Ensure prefix ends with "/" before appending universe UUID and node name
          if (!s3Prefix.endsWith("/")) {
            s3Prefix = s3Prefix + "/";
          }
          s3Prefix = s3Prefix + universeUUID.toString() + "/" + nodeName;
        }
        s3UploaderConfig.setS3_prefix(s3Prefix);

        s3UploaderConfig.setS3_partition(s3Config.getPartition().getGranularity());
        s3UploaderConfig.setRole_arn(s3Config.getRoleArn());
        s3UploaderConfig.setFile_prefix(s3Config.getFilePrefix());
        s3UploaderConfig.setRegion(s3Config.getRegion());
        s3UploaderConfig.setEndpoint(s3Config.getEndpoint());
        s3UploaderConfig.setS3_force_path_style(s3Config.getForcePathStyle());
        s3UploaderConfig.setDisable_ssl(s3Config.getDisableSSL());
        s3Exporter.setS3uploader(s3UploaderConfig);

        exporterName = EXPORTER_PREFIX_S3 + exportTypeAndUUIDString;

        exporters.put(exporterName, setExporterCommonConfig(s3Exporter, false, false, exportType));
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
        exporterName = EXPORTER_PREFIX_GCP_CLOUD_MONITORING + exportTypeAndUUIDString;
        // TODO add retry config to GCP provider once it's supported by Otel Collector
        exporters.put(
            exporterName,
            setExporterCommonConfig(gcpCloudMonitoringExporter, true, false, exportType));
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
        if (lokiConfig.getAuthType() == AuthType.BasicAuth) {
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
        exporterName = EXPORTER_PREFIX_LOKI + exportTypeAndUUIDString;
        exporters.put(exporterName, setExporterCommonConfig(lokiExporter, true, true, exportType));
        break;
      case OTLP:
        OTLPConfig otlpConfig = (OTLPConfig) telemetryProvider.getConfig();
        OtelCollectorConfigFormat.OTLPExporter otlpExporter =
            new OtelCollectorConfigFormat.OTLPExporter();

        otlpExporter.setEndpoint(otlpConfig.getEndpoint());
        otlpExporter.setTimeout(otlpConfig.getTimeoutSeconds() + "s");
        otlpExporter.setCompression(otlpConfig.getCompression().name());
        otlpExporter.setHeaders(otlpConfig.getHeaders());

        OtelCollectorConfigFormat.TlsSettings otlpTLSSettings =
            new OtelCollectorConfigFormat.TlsSettings();
        otlpTLSSettings.setInsecure_skip_verify(true);
        otlpExporter.setTls(otlpTLSSettings);

        if (StringUtils.isNotBlank(otlpConfig.getLogsEndpoint())) {
          otlpExporter.setLogs_endpoint(otlpConfig.getLogsEndpoint());
        }
        if (StringUtils.isNotBlank(otlpConfig.getMetricsEndpoint())) {
          otlpExporter.setMetrics_endpoint(otlpConfig.getMetricsEndpoint());
        }

        // Add authentication extension if auth type is not NoAuth
        String authExtensionName;
        switch (otlpConfig.getAuthType()) {
          case BasicAuth:
            OtelCollectorConfigFormat.BasicAuthExtension basicAuthExt =
                new OtelCollectorConfigFormat.BasicAuthExtension();
            OtelCollectorConfigFormat.ClientAuth clientAuth =
                new OtelCollectorConfigFormat.ClientAuth();
            clientAuth.setUsername(otlpConfig.getBasicAuth().getUsername());
            clientAuth.setPassword(otlpConfig.getBasicAuth().getPassword());
            basicAuthExt.setClient_auth(clientAuth);

            authExtensionName = "basicauth/" + exportTypeAndUUIDString;
            extensions.put(authExtensionName, basicAuthExt);

            // Set the auth reference in the exporter
            OtelCollectorConfigFormat.AuthConfig basicAuthConfig =
                new OtelCollectorConfigFormat.AuthConfig();
            basicAuthConfig.setAuthenticator(authExtensionName);
            otlpExporter.setAuth(basicAuthConfig);
            setExtension = true;
            break;

          case BearerToken:
            OtelCollectorConfigFormat.BearerTokenAuthExtension bearerAuthExt =
                new OtelCollectorConfigFormat.BearerTokenAuthExtension();
            bearerAuthExt.setBearer_token(otlpConfig.getBearerToken().getToken());

            authExtensionName = "bearertoken/" + exportTypeAndUUIDString;
            extensions.put(authExtensionName, bearerAuthExt);

            // Set the auth reference in the exporter
            OtelCollectorConfigFormat.AuthConfig bearerAuthConfig =
                new OtelCollectorConfigFormat.AuthConfig();
            bearerAuthConfig.setAuthenticator(authExtensionName);
            otlpExporter.setAuth(bearerAuthConfig);
            setExtension = true;
            break;

          case NoAuth:
            // No authentication extension needed
            break;

          default:
            throw new IllegalArgumentException(
                "Unsupported auth type: " + otlpConfig.getAuthType());
        }

        String exporterTypePrefix = otlpConfig.getProtocol().getExporterType();

        exporterName = exporterTypePrefix + "/" + exportTypeAndUUIDString;
        exporters.put(
            exporterName,
            setExporterCommonConfig(
                otlpExporter, true, true, exportType, otlpConfig.getRetryOnFailure()));

        break;
      default:
        throw new IllegalArgumentException(
            "Exporter type "
                + telemetryProvider.getConfig().getType().name()
                + " is not supported.");
    }

    // Update service extensions to include any dynamically added extensions (e.g., auth
    // extensions)
    if (collectorConfig.getService() != null && setExtension) {
      collectorConfig
          .getService()
          .setExtensions(new ArrayList<>(collectorConfig.getExtensions().keySet()));
    }

    return exporterName;
  }

  private OtelCollectorConfigFormat.Exporter setExporterCommonConfig(
      OtelCollectorConfigFormat.Exporter exporter,
      boolean setQueueEnabled,
      boolean setRetryOnFailure,
      ExportType exportType) {
    return setExporterCommonConfig(exporter, setQueueEnabled, setRetryOnFailure, exportType, null);
  }

  private OtelCollectorConfigFormat.Exporter setExporterCommonConfig(
      OtelCollectorConfigFormat.Exporter exporter,
      boolean setQueueEnabled,
      boolean setRetryOnFailure,
      ExportType exportType,
      ExporterRetryConfig apiRetry) {
    // Set retry on failure with defaults / requested values.
    if (setRetryOnFailure) {
      RetryConfig retryConfig = buildRetryConfig(exportType, apiRetry);
      exporter.setRetry_on_failure(retryConfig);
    } else {
      exporter.setRetry_on_failure(null);
    }

    // Set sending queue, only for DBAL and not other exports due to memory constraints.
    if (exportType == ExportType.AUDIT_LOGS) {
      OtelCollectorConfigFormat.QueueConfig queueConfig =
          new OtelCollectorConfigFormat.QueueConfig();
      if (setQueueEnabled) {
        queueConfig.setEnabled(true);
        queueConfig.setStorage("file_storage/queue");
        exporter.setSending_queue(queueConfig);
      } else {
        exporter.setSending_queue(null);
      }
    }
    return exporter;
  }

  /**
   * Builds Otel RetryConfig from export-type defaults and applies API-provided overrides if
   * present.
   */
  private RetryConfig buildRetryConfig(ExportType exportType, ExporterRetryConfig apiRetry) {
    RetryConfig retryConfig = new RetryConfig();

    // Apply YBA set defaults.
    retryConfig.setEnabled(true);
    switch (exportType) {
      case AUDIT_LOGS:
        retryConfig.setInitial_interval("1m");
        retryConfig.setMax_interval("1800m");
        retryConfig.setMax_elapsed_time("1800m");
        break;
      default:
        retryConfig.setInitial_interval("30s");
        retryConfig.setMax_interval("10m");
        retryConfig.setMax_elapsed_time("60m");
        break;
    }

    // Override with API-provided values if present.
    return ExporterRetryConfig.applyOverrides(retryConfig, apiRetry);
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

  private void addAwsCredentialsToSecretEnv(
      String accessKey, String secretKey, List<Object> secretEnv) {
    if (StringUtils.isNotEmpty(accessKey)) {
      String encodedAccessKey = Base64.getEncoder().encodeToString(accessKey.getBytes());
      secretEnv.add(ImmutableMap.of("envName", "AWS_ACCESS_KEY_ID", "envValue", encodedAccessKey));
    }
    if (StringUtils.isNotEmpty(secretKey)) {
      String encodedSecretKey = Base64.getEncoder().encodeToString(secretKey.getBytes());
      secretEnv.add(
          ImmutableMap.of("envName", "AWS_SECRET_ACCESS_KEY", "envValue", encodedSecretKey));
    }
  }

  private void appendSecretEnv(TelemetryProvider telemetryProvider, List<Object> secretEnv) {
    switch (telemetryProvider.getConfig().getType()) {
      case AWS_CLOUDWATCH:
        AWSCloudWatchConfig awsCloudWatchConfig =
            (AWSCloudWatchConfig) telemetryProvider.getConfig();
        addAwsCredentialsToSecretEnv(
            awsCloudWatchConfig.getAccessKey(), awsCloudWatchConfig.getSecretKey(), secretEnv);
        break;
      case S3:
        S3Config s3Config = (S3Config) telemetryProvider.getConfig();
        addAwsCredentialsToSecretEnv(s3Config.getAccessKey(), s3Config.getSecretKey(), secretEnv);
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

  private void addCommonTransformProcessor(OtelCollectorConfigFormat collectorConfig) {
    String transformProcessorName = PROCESSOR_PREFIX_TRANSFORM + "replace";

    // Only add the transform processor if it doesn't already exist
    if (!collectorConfig.getProcessors().containsKey(transformProcessorName)) {
      OtelCollectorConfigFormat.TransformProcessor transformProcessor =
          new OtelCollectorConfigFormat.TransformProcessor();
      OtelCollectorConfigFormat.LogStatement logStatement =
          new OtelCollectorConfigFormat.LogStatement();
      logStatement.setContext("log");
      logStatement.setStatements(ImmutableList.of("replace_pattern(body, \"^otho8Aut\", \"\")"));
      transformProcessor.setLog_statements(ImmutableList.of(logStatement));
      collectorConfig.getProcessors().put(transformProcessorName, transformProcessor);
    }
  }

  private String exportTypeAndUUID(UUID telemetryProviderUUID, ExportType exportType) {
    String exportTypeAndUUID;
    switch (exportType) {
      case AUDIT_LOGS:
        exportTypeAndUUID = telemetryProviderUUID.toString();
        break;
      case QUERY_LOGS:
        exportTypeAndUUID = EXPORT_TYPE_PREFIX_QUERY_LOGS + telemetryProviderUUID;
        break;
      case METRICS:
        exportTypeAndUUID = EXPORT_TYPE_PREFIX_METRICS + telemetryProviderUUID;
        break;
      default:
        throw new IllegalArgumentException("Unsupported export type: " + exportType);
    }
    return exportTypeAndUUID;
  }
}
