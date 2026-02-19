package com.yugabyte.yw.common.audit.otel;

import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.NoArgsConstructor;

@Data
public class OtelCollectorConfigFormat {
  private Map<String, Receiver> receivers = new LinkedHashMap<>();
  private Map<String, Processor> processors = new LinkedHashMap<>();
  private Map<String, Exporter> exporters = new LinkedHashMap<>();
  private Map<String, Extension> extensions = new LinkedHashMap<>();
  private Service service;

  @Data
  public static class Receiver {}

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class FileLogReceiver extends Receiver {
    private List<String> include;
    private List<String> exclude;
    private String start_at;
    private String storage;
    private MultilineConfig multiline;
    private List<Operator> operators;
    private Map<String, String> attributes;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class PrometheusReceiver extends Receiver {
    private PrometheusConfig config;
  }

  @Data
  public static class PrometheusConfig {
    private GlobalConfig global;
    private List<ScrapeConfig> scrape_configs;
  }

  @Data
  public static class GlobalConfig {
    private String scrape_interval;
    private String scrape_timeout;
  }

  @Data
  public static class ScrapeConfig {
    private String job_name;
    private String scheme;
    private String metrics_path;
    private List<StaticConfig> static_configs;
    private List<MetricRelabelConfig> metric_relabel_configs;
    private TlsConfig tls_config;
  }

  @Data
  public static class StaticConfig {
    private List<String> targets;
    private Map<String, String> labels;
  }

  @Data
  public static class MetricRelabelConfig {
    private List<String> source_labels;
    private String regex;
    private String target_label;
    private String replacement;
  }

  @Data
  public static class TlsConfig {
    private boolean insecure_skip_verify;
  }

  @Data
  public static class Operator {
    private String type;
  }

  @Data
  public static class MultilineConfig {
    private String line_start_pattern;
    private String line_end_pattern;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class RegexOperator extends Operator {
    private String regex;
    private String on_error;
    private OperatorTimestamp timestamp;
    private OperatorSeverity severity;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class FilterOperator extends Operator {
    private String expr;
  }

  @Data
  public static class OperatorTimestamp {
    private String parse_from;
    private String layout_type;
    private String layout;
  }

  @Data
  public static class OperatorSeverity {
    private String parse_from;
  }

  @Data
  public static class Processor {}

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class FilterProcessor extends Processor {
    private FilterProcessorLogsConfig logs;
    private String error_mode;
  }

  @Data
  public static class FilterProcessorLogsConfig {
    FilterIncludeExclude include;
    FilterIncludeExclude exclude;
  }

  @Data
  public static class FilterIncludeExclude {
    private String match_type;
    private List<String> bodies;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class AttributesProcessor extends Processor {
    private List<AttributeAction> actions;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class BatchProcessor extends Processor {
    private int send_batch_max_size;
    private int send_batch_size;
    private String timeout;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class MemoryLimiterProcessor extends Processor {
    private String check_interval;
    private int limit_mib;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class CumulativeToDeltaProcessor extends Processor {
    // No additional configuration needed for basic cumulative to delta conversion
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class MetricTransformProcessor extends Processor {
    private List<MetricTransformRule> transforms;
  }

  @Data
  public static class MetricTransformRule {
    private String include;
    private String match_type;
    private String action;
    private Map<String, String> experimental_match_labels;
    private String new_name;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class TransformProcessor extends Processor {
    private List<LogStatement> log_statements;
  }

  @Data
  public static class LogStatement {
    private String context;
    private List<String> statements;
  }

  @Data
  @AllArgsConstructor
  @NoArgsConstructor
  public static class AttributeAction {
    private String key;
    private String value;
    private String action;
    private String from_attribute;
  }

  @Data
  public static class Exporter {
    private RetryConfig retry_on_failure;
    private QueueConfig sending_queue;
    private TlsSettings tls;
  }

  @Data
  public static class RetryConfig {
    private boolean enabled;
    private String initial_interval;
    private String max_interval;
    private String max_elapsed_time;
  }

  @Data
  public static class QueueConfig {
    private boolean enabled;
    private String storage;
  }

  @Data
  public static class TlsSettings {
    private boolean insecure_skip_verify;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class DataDogExporter extends Exporter {
    private DataDogApiConfig api;
  }

  @Data
  public static class DataDogApiConfig {
    private String site;
    private String key;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class DynatraceExporter extends Exporter {
    private String endpoint;
    private Map<String, String> headers;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class LokiExporter extends Exporter {
    private String endpoint;
    private Map<String, String> headers;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class OTLPExporter extends Exporter {
    private String endpoint;
    private String compression;
    private Map<String, String> headers;
    private TlsSettings tls;
    private String timeout;
    private String logs_endpoint;
    private String metrics_endpoint;
    private AuthConfig auth;
  }

  @Data
  public static class AuthConfig {
    private String authenticator;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class SplunkExporter extends Exporter {
    private String token;
    private String endpoint;
    private String source;
    private String sourcetype;
    private String index;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class AWSCloudWatchExporter extends Exporter {
    private String log_group_name;
    private String log_stream_name;
    private String region;
    private String endpoint;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class AWSS3Exporter extends Exporter {
    private String marshaler;
    private S3UploaderConfig s3uploader;
  }

  @Data
  public static class S3UploaderConfig {
    private String endpoint;
    private String s3_bucket;
    private String region;
    private String s3_prefix;
    private String s3_partition;
    private String role_arn;
    private String file_prefix;
    private Boolean s3_force_path_style;
    private Boolean disable_ssl;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class GCPCloudMonitoringExporter extends Exporter {
    private String project;
    private GCPCloudMonitoringLog log;
  }

  @Data
  public static class GCPCloudMonitoringLog {
    private String default_log_name;
  }

  @Data
  public static class Extension {}

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class StorageExtension extends Extension {
    private String directory;
    private String timeout;
    private StorageCompaction compaction;
  }

  @Data
  public static class StorageCompaction {
    private String directory;
    private boolean on_start;
    private boolean on_rebound;
    private int rebound_needed_threshold_mib;
    private int rebound_trigger_threshold_mib;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class BasicAuthExtension extends Extension {
    private ClientAuth client_auth;
  }

  @Data
  public static class ClientAuth {
    private String username;
    private String password;
  }

  @Data
  @EqualsAndHashCode(callSuper = true)
  public static class BearerTokenAuthExtension extends Extension {
    private String bearer_token;
  }

  @Data
  public static class Service {
    private List<String> extensions;
    private Map<String, Pipeline> pipelines = new HashMap<>();
    private TelemetryConfig telemetry;
  }

  @Data
  public static class TelemetryConfig {
    private LogsConfig logs;
    private MetricsConfig metrics;
  }

  @Data
  public static class LogsConfig {
    private List<String> output_paths;
  }

  @Data
  public static class Pipeline {
    private List<String> receivers;
    private List<String> processors;
    private List<String> exporters;
  }

  @Data
  public static class MetricsConfig {
    private String address;
  }
}
