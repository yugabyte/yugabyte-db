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
  @AllArgsConstructor
  @NoArgsConstructor
  public static class AttributeAction {
    private String key;
    private String value;
    private String action;
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
  public static class Service {
    private List<String> extensions;
    private Map<String, Pipeline> pipelines = new HashMap<>();
    private TelemetryConfig telemetry;
  }

  @Data
  public static class TelemetryConfig {
    private LogsConfig logs;
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
}
