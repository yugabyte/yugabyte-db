// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfigurationThreshold.Condition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.Unit;
import java.util.EnumSet;
import java.util.Map;
import lombok.Getter;
import lombok.Value;

@Getter
public enum AlertTemplate {

  // @formatter:off
  REPLICATION_LAG(
      "Replication Lag",
      "Average universe replication lag for 10 minutes in ms is above threshold",
      "max by (node_prefix) (avg_over_time(async_replication_committed_lag_micros"
          + "{node_prefix=\"__nodePrefix__\"}[10m]) "
          + "or avg_over_time(async_replication_sent_lag_micros"
          + "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 "
          + "{{ query_condition }} {{ query_threshold }}",
      "Average replication lag for universe '{{ $labels.source_name }}'"
          + " is above {{ $labels.threshold }} ms."
          + " Current value is {{ $value | printf \\\"%.0f\\\" }} ms",
      15,
      EnumSet.noneOf(DefinitionSettings.class),
      ImmutableMap.of(
          AlertConfiguration.Severity.SEVERE, DefaultThreshold.from("yb.alert.replication_lag_ms")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.MILLISECOND),

  CLOCK_SKEW(
      "Clock Skew",
      "Max universe clock skew in ms is above threshold during last 10 minutes",
      "max by (node_prefix) (max_over_time(hybrid_clock_skew"
          + "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 "
          + "{{ query_condition }} {{ query_threshold }}",
      "Max clock skew for universe '{{ $labels.source_name }}'"
          + " is above {{ $labels.threshold }} ms."
          + " Current value is {{ $value | printf \\\"%.0f\\\" }} ms",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          AlertConfiguration.Severity.SEVERE, DefaultThreshold.from("yb.alert.max_clock_skew_ms")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.MILLISECOND),

  MEMORY_CONSUMPTION(
      "Memory Consumption",
      "Average node memory consumption percentage for 10 minutes is above threshold",
      "(max by (node_prefix)"
          + "   (avg_over_time(node_memory_MemTotal{node_prefix=\"__nodePrefix__\"}[10m])) -"
          + " max by (node_prefix)"
          + "   (avg_over_time(node_memory_Buffers{node_prefix=\"__nodePrefix__\"}[10m])) -"
          + " max by (node_prefix)"
          + "   (avg_over_time(node_memory_Cached{node_prefix=\"__nodePrefix__\"}[10m])) -"
          + " max by (node_prefix)"
          + "   (avg_over_time(node_memory_MemFree{node_prefix=\"__nodePrefix__\"}[10m])) -"
          + " max by (node_prefix)"
          + "   (avg_over_time(node_memory_Slab{node_prefix=\"__nodePrefix__\"}[10m]))) /"
          + " (max by (node_prefix)"
          + "   (avg_over_time(node_memory_MemTotal{node_prefix=\"__nodePrefix__\"}[10m])))"
          + " * 100 {{ query_condition }} {{ query_threshold }}",
      "Average memory usage for universe '{{ $labels.source_name }}'"
          + " is above {{ $labels.threshold }}%."
          + " Current value is {{ $value | printf \\\"%.0f\\\" }}%",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          AlertConfiguration.Severity.SEVERE,
          DefaultThreshold.from("yb.alert.max_memory_cons_pct")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.PERCENT),

  HEALTH_CHECK_ERROR(
      "Health Check Error",
      "Failed to perform health check",
      "ybp_health_check_status{universe_uuid = \"__universeUuid__\"} {{ query_condition }} 1",
      "Failed to perform health check for universe '{{ $labels.source_name }}': "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertConfiguration.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.LESS_THAN,
      Unit.STATUS),

  HEALTH_CHECK_NOTIFICATION_ERROR(
      "Health Check Notification Error",
      "Failed to perform health check notification",
      "ybp_health_check_notification_status{universe_uuid = \"__universeUuid__\"}"
          + " {{ query_condition }} 1",
      "Failed to perform health check notification for universe '{{ $labels.source_name }}': "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertConfiguration.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.LESS_THAN,
      Unit.STATUS),

  BACKUP_FAILURE(
      "Backup Failure",
      "Last universe backup creation task failed",
      "ybp_create_backup_status{universe_uuid = \"__universeUuid__\"}" + " {{ query_condition }} 1",
      "Last backup task for universe '{{ $labels.source_name }}' failed: "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertConfiguration.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.LESS_THAN,
      Unit.STATUS),

  BACKUP_SCHEDULE_FAILURE(
      "Backup Schedule Failure",
      "Last attempt to run scheduled backup failed due to other backup"
          + " or universe operation in progress",
      "ybp_schedule_backup_status{universe_uuid = \"__universeUuid__\"}"
          + " {{ query_condition }} 1",
      "Last attempt to run scheduled backup for universe '{{ $labels.source_name }}'"
          + " failed due to other backup or universe operation is in progress.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertConfiguration.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.LESS_THAN,
      Unit.STATUS),

  INACTIVE_CRON_NODES(
      "Inactive Cronjob Nodes",
      "Number of nodes with inactive cronjob is above threshold",
      "ybp_universe_inactive_cron_nodes{universe_uuid = \"__universeUuid__\"}"
          + " {{ query_condition }} {{ query_threshold }}",
      "{{ $value | printf \\\"%.0f\\\" }} node(s) has inactive cronjob"
          + " for universe '{{ $labels.source_name }}'.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          AlertConfiguration.Severity.SEVERE,
          DefaultThreshold.from("yb.alert.inactive_cronjob_nodes")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  ALERT_QUERY_FAILED(
      "Alert Query Failed",
      "Failed to query alerts from Prometheus",
      "ybp_alert_query_status {{ query_condition }} 1",
      "Last alert query for customer '{{ $labels.source_name }}' failed: "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertConfiguration.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertConfiguration.TargetType.PLATFORM,
      Condition.LESS_THAN,
      Unit.STATUS),

  ALERT_CONFIG_WRITING_FAILED(
      "Alert Rules Sync Failed",
      "Failed to sync alerting rules to Prometheus",
      "ybp_alert_config_writer_status {{ query_condition }} 1",
      "Last alert rules sync for customer '{{ $labels.source_name }}' failed: "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertConfiguration.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertConfiguration.TargetType.PLATFORM,
      Condition.LESS_THAN,
      Unit.STATUS),

  ALERT_NOTIFICATION_ERROR(
      "Alert Notification Failed",
      "Failed to send alert notifications",
      "ybp_alert_manager_status{customer_uuid = \"__customerUuid__\"}" + " {{ query_condition }} 1",
      "Last attempt to send alert notifications for customer '{{ $labels.source_name }}'"
          + " failed: {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertConfiguration.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertConfiguration.TargetType.PLATFORM,
      Condition.LESS_THAN,
      Unit.STATUS),

  ALERT_NOTIFICATION_CHANNEL_ERROR(
      "Alert Channel Failed",
      "Failed to send alerts to notification channel",
      "ybp_alert_manager_channel_status{customer_uuid = \"__customerUuid__\"}"
          + " {{ query_condition }} 1",
      "Last attempt to send alert notifications to channel '{{ $labels.source_name }}'"
          + " failed: {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER, DefinitionSettings.SKIP_TARGET_LABELS),
      ImmutableMap.of(AlertConfiguration.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertConfiguration.TargetType.PLATFORM,
      Condition.LESS_THAN,
      Unit.STATUS),

  NODE_DOWN(
      "DB node down",
      "DB node is down for 15 minutes",
      "count by (node_prefix) (max_over_time("
          + "up{export_type=\"node_export\","
          + "node_prefix=\"__nodePrefix__\"}[15m]) < 1) "
          + "{{ query_condition }} {{ query_threshold }}",
      "{{ $value | printf \\\"%.0f\\\" }} DB node(s) are down "
          + "for more than 15 minutes for universe '{{ $labels.source_name }}'.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(Severity.SEVERE, DefaultThreshold.from(0D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  NODE_RESTART(
      "DB node restart",
      "Unexpected DB node restart(s) occurred during last 30 minutes",
      "max by (node_prefix) "
          + "(changes(node_boot_time{node_prefix=\"__nodePrefix__\"}[30m])) "
          + "{{ query_condition }} {{ query_threshold }}",
      "Universe '{{ $labels.source_name }}'"
          + " DB node is restarted  {{ $value | printf \\\"%.0f\\\" }} times"
          + " during last 30 minutes",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.WARNING, DefaultThreshold.from(0D),
          Severity.SEVERE, DefaultThreshold.from(2D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  NODE_CPU_USAGE(
      "DB node CPU usage",
      "Average node CPU usage percentage for 30 minutes is above threshold",
      "count by(node_prefix) "
          + " ((100 - (avg by (node_prefix, instance)"
          + " (avg_over_time(irate(node_cpu{job=\"node\",mode=\"idle\","
          + " node_prefix=\"__nodePrefix__\"}[1m])[30m:])) * 100)) "
          + "{{ query_condition }} {{ query_threshold }})",
      "Average node CPU usage for universe '{{ $labels.source_name }}'"
          + " is above {{ $labels.threshold }}% on {{ $value | printf \\\"%.0f\\\" }} node(s).",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.WARNING, DefaultThreshold.from("yb.alert.max_cpu_usage_pct_warn"),
          Severity.SEVERE, DefaultThreshold.from("yb.alert.max_cpu_usage_pct_severe")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.PERCENT),

  NODE_DISK_USAGE(
      "DB node disk usage",
      "Node Disk usage percentage is above threshold",
      "count by (node_prefix) (100 - (sum without (saved_name) "
          + "(node_filesystem_free{mountpoint=~\"/mnt/.*\", node_prefix=\"__nodePrefix__\"}) "
          + "/ sum without (saved_name) "
          + "(node_filesystem_size{mountpoint=~\"/mnt/.*\", node_prefix=\"__nodePrefix__\"}) "
          + "* 100) {{ query_condition }} {{ query_threshold }})",
      "Node disk usage for universe '{{ $labels.source_name }}'"
          + " is above {{ $labels.threshold }}% on {{ $value | printf \\\"%.0f\\\" }} node(s).",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.SEVERE, DefaultThreshold.from("yb.alert.max_node_disk_usage_pct_severe")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.PERCENT),

  NODE_FILE_DESCRIPTORS_USAGE(
      "DB node file descriptors usage",
      "Node file descriptors usage percentage is above threshold",
      "count by (universe_uuid) (ybp_health_check_used_fd_pct{"
          + "universe_uuid=\"__universeUuid__\"} * 100 "
          + "{{ query_condition }} {{ query_threshold }})",
      "Node file descriptors usage for universe '{{ $labels.source_name }}'"
          + " is above {{ $labels.threshold }}% on {{ $value | printf \\\"%.0f\\\" }} node(s).",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.SEVERE, DefaultThreshold.from("yb.alert.max_node_fd_usage_pct_severe")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.PERCENT),

  DB_VERSION_MISMATCH(
      "DB version mismatch",
      "DB Master/TServer version does not match Platform universe version",
      "ybp_health_check_tserver_version_mismatch{universe_uuid=\"__universeUuid__\"} "
          + "+ ybp_health_check_master_version_mismatch{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }}",
      "Version mismatch detected for universe '{{ $labels.source_name }}'"
          + " for {{ $value | printf \\\"%.0f\\\" }} Master/TServer instance(s).",
      3600,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(Severity.SEVERE, DefaultThreshold.from(0D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  DB_INSTANCE_DOWN(
      "DB instance down",
      "DB Master/TServer instance is down for 15 minutes",
      "count by (node_prefix) (max_over_time("
          + "up{export_type=~\"master_export|tserver_export\","
          + "node_prefix=\"__nodePrefix__\"}[15m]) < 1) "
          + "{{ query_condition }} {{ query_threshold }}",
      "{{ $value | printf \\\"%.0f\\\" }} DB Master/TServer instance(s) are down "
          + "for more than 15 minutes for universe '{{ $labels.source_name }}'.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(Severity.SEVERE, DefaultThreshold.from(0D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  DB_INSTANCE_RESTART(
      "DB Instance restart",
      "Unexpected Master or TServer process restart(s) occurred during last 30 minutes",
      "max by (universe_uuid) "
          + "(changes(ybp_health_check_master_boot_time_sec{"
          + "universe_uuid=\"__universeUuid__\"}[30m])) "
          + "{{ query_condition }} {{ query_threshold }}",
      "Universe '{{ $labels.source_name }}'"
          + " Master or TServer is restarted {{ $value | printf \\\"%.0f\\\" }} times"
          + " during last 30 minutes",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.WARNING, DefaultThreshold.from(0D),
          Severity.SEVERE, DefaultThreshold.from(2D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  DB_FATAL_LOGS(
      "DB fatal logs",
      "Fatal logs detected on DB Master/TServer instances",
      "ybp_health_check_master_fatal_logs{universe_uuid=\"__universeUuid__\"} "
          + "+ ybp_health_check_tserver_fatal_logs{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }}",
      "Fatal logs detected for universe '{{ $labels.source_name }}'"
          + " on {{ $value | printf \\\"%.0f\\\" }} Master/TServer instance(s).",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(Severity.SEVERE, DefaultThreshold.from(0D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  DB_CORE_FILES(
      "DB core files",
      "Core files detected on DB TServer instances",
      "ybp_health_check_tserver_core_files{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }}",
      "Core files detected for universe '{{ $labels.source_name }}'"
          + " on {{ $value | printf \\\"%.0f\\\" }} TServer instance(s).",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(Severity.SEVERE, DefaultThreshold.from(0D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  DB_YSQL_CONNECTION(
      "DB YSQLSH connection",
      "YSQLSH connection to DB instances failed",
      "ybp_health_check_ysqlsh_connectivity_error{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }}",
      "YSQLSH connection failure detected for universe '{{ $labels.source_name }}'"
          + " on {{ $value | printf \\\"%.0f\\\" }} TServer instance(s).",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(Severity.SEVERE, DefaultThreshold.from(0D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  DB_YCQL_CONNECTION(
      "DB CQLSH connection",
      "CQLSH connection to DB instances failed",
      "ybp_health_check_cqlsh_connectivity_error{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }}",
      "CQLSH connection failure detected for universe '{{ $labels.source_name }}'"
          + " on {{ $value | printf \\\"%.0f\\\" }} TServer instance(s).",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(Severity.SEVERE, DefaultThreshold.from(0D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  DB_REDIS_CONNECTION(
      "DB Redis connection",
      "Redis connection to DB instances failed",
      "ybp_health_check_redis_connectivity_error{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }}",
      "Redis connection failure detected for universe '{{ $labels.source_name }}'"
          + " on {{ $value | printf \\\"%.0f\\\" }} TServer instance(s).",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(Severity.SEVERE, DefaultThreshold.from(0D)),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.GREATER_THAN,
      Unit.COUNT),

  NODE_TO_NODE_CA_CERT_EXPIRY(
      "Node to node CA cert expiry",
      "Node to node CA certificate expires soon",
      "min by (node_name) (ybp_health_check_n2n_ca_cert_validity_days"
          + "{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }})",
      "Node to node CA certificate for universe '{{ $labels.source_name }}'"
          + " will expire in {{ $value | printf \\\"%.0f\\\" }} days.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.SEVERE, DefaultThreshold.from("yb.alert.max_node_cert_expiry_days_severe")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.LESS_THAN,
      Unit.DAY),

  NODE_TO_NODE_CERT_EXPIRY(
      "Node to node cert expiry",
      "Node to node certificate expires soon",
      "min by (node_name) (ybp_health_check_n2n_cert_validity_days"
          + "{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }})",
      "Node to node certificate for universe '{{ $labels.source_name }}'"
          + " will expire in {{ $value | printf \\\"%.0f\\\" }} days.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.SEVERE, DefaultThreshold.from("yb.alert.max_node_cert_expiry_days_severe")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.LESS_THAN,
      Unit.DAY),

  CLIENT_TO_NODE_CA_CERT_EXPIRY(
      "Client to node CA cert expiry",
      "Client to node CA certificate expires soon",
      "min by (node_name) (ybp_health_check_c2n_ca_cert_validity_days"
          + "{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }})",
      "Client to node CA certificate for universe '{{ $labels.source_name }}'"
          + " will expire in {{ $value | printf \\\"%.0f\\\" }} days.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.SEVERE, DefaultThreshold.from("yb.alert.max_node_cert_expiry_days_severe")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.LESS_THAN,
      Unit.DAY),

  CLIENT_TO_NODE_CERT_EXPIRY(
      "Client to node cert expiry",
      "Client to node certificate expires soon",
      "min by (node_name) (ybp_health_check_c2n_cert_validity_days"
          + "{universe_uuid=\"__universeUuid__\"} "
          + "{{ query_condition }} {{ query_threshold }})",
      "Client to node certificate for universe '{{ $labels.source_name }}'"
          + " will expire in {{ $value | printf \\\"%.0f\\\" }} days.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          Severity.SEVERE, DefaultThreshold.from("yb.alert.max_node_cert_expiry_days_severe")),
      AlertConfiguration.TargetType.UNIVERSE,
      Condition.LESS_THAN,
      Unit.DAY);
  // @formatter:on

  enum DefinitionSettings {
    CREATE_FOR_NEW_CUSTOMER,
    SKIP_TARGET_LABELS
  }

  private final String name;

  private final String description;

  private final String queryTemplate;

  private final String summaryTemplate;

  private final int defaultDurationSec;

  private final EnumSet<DefinitionSettings> settings;

  private final Map<AlertConfiguration.Severity, DefaultThreshold> defaultThresholdMap;

  private final AlertConfiguration.TargetType targetType;

  private final Condition defaultThresholdCondition;

  private final Unit defaultThresholdUnit;

  private final double thresholdMinValue;

  private final double thresholdMaxValue;

  public String buildTemplate(Customer customer) {
    return buildTemplate(customer, null);
  }

  public String buildTemplate(Customer customer, Universe universe) {
    String query = queryTemplate.replaceAll("__customerUuid__", customer.getUuid().toString());
    if (universe != null) {
      query =
          query
              .replaceAll("__nodePrefix__", universe.getUniverseDetails().nodePrefix)
              .replaceAll("__universeUuid__", universe.getUniverseUUID().toString());
    }
    return query;
  }

  AlertTemplate(
      String name,
      String description,
      String queryTemplate,
      String summaryTemplate,
      int defaultDurationSec,
      EnumSet<DefinitionSettings> settings,
      Map<AlertConfiguration.Severity, DefaultThreshold> defaultThresholdParamMap,
      AlertConfiguration.TargetType targetType,
      Condition defaultThresholdCondition,
      Unit defaultThresholdUnit) {
    this(
        name,
        description,
        queryTemplate,
        summaryTemplate,
        defaultDurationSec,
        settings,
        defaultThresholdParamMap,
        targetType,
        defaultThresholdCondition,
        defaultThresholdUnit,
        defaultThresholdUnit.getMinValue(),
        defaultThresholdUnit.getMaxValue());
  }

  AlertTemplate(
      String name,
      String description,
      String queryTemplate,
      String summaryTemplate,
      int defaultDurationSec,
      EnumSet<DefinitionSettings> settings,
      Map<AlertConfiguration.Severity, DefaultThreshold> defaultThresholdParamMap,
      AlertConfiguration.TargetType targetType,
      Condition defaultThresholdCondition,
      Unit defaultThresholdUnit,
      double thresholdMinValue,
      double thresholdMaxValue) {
    this.name = name;
    this.description = description;
    this.queryTemplate = queryTemplate;
    this.summaryTemplate = summaryTemplate;
    this.defaultDurationSec = defaultDurationSec;
    this.settings = settings;
    this.defaultThresholdMap = defaultThresholdParamMap;
    this.targetType = targetType;
    this.defaultThresholdCondition = defaultThresholdCondition;
    this.defaultThresholdUnit = defaultThresholdUnit;
    this.thresholdMinValue = thresholdMinValue;
    this.thresholdMaxValue = thresholdMaxValue;
  }

  public boolean isCreateForNewCustomer() {
    return settings.contains(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER);
  }

  public boolean isSkipTargetLabels() {
    return settings.contains(DefinitionSettings.SKIP_TARGET_LABELS);
  }

  @Value
  public static class DefaultThreshold {

    private static final double STATUS_OK_THRESHOLD = 1;

    String paramName;
    Double threshold;

    private static DefaultThreshold from(String paramName) {
      return new DefaultThreshold(paramName, null);
    }

    private static DefaultThreshold from(Double threshold) {
      return new DefaultThreshold(null, threshold);
    }

    private static DefaultThreshold statusOk() {
      return from(STATUS_OK_THRESHOLD);
    }

    public boolean isParamName() {
      return paramName != null;
    }
  }
}
