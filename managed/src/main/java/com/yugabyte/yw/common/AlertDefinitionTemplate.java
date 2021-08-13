// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertDefinitionGroupThreshold;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.Unit;
import java.util.EnumSet;
import java.util.Map;
import lombok.Getter;
import lombok.Value;

@Getter
public enum AlertDefinitionTemplate {

  // @formatter:off
  REPLICATION_LAG(
      "Replication Lag",
      "Average universe replication lag for 10 minutes in ms is above threshold",
      "max by (node_prefix) (avg_over_time(async_replication_committed_lag_micros"
          + "{node_prefix=\"__nodePrefix__\"}[10m]) "
          + "or avg_over_time(async_replication_sent_lag_micros"
          + "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 "
          + "{{ query_condition }} {{ query_threshold }}",
      "Average replication lag for universe '{{ $labels.target_name }}'"
          + " is above {{ $labels.threshold }} ms."
          + " Current value is {{ $value | printf \\\"%.0f\\\" }} ms",
      15,
      EnumSet.noneOf(DefinitionSettings.class),
      ImmutableMap.of(
          AlertDefinitionGroup.Severity.SEVERE,
          DefaultThreshold.from("yb.alert.replication_lag_ms")),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.GREATER_THAN,
      Unit.MILLISECOND),

  CLOCK_SKEW(
      "Clock Skew",
      "Max universe clock skew in ms is above threshold during last 10 minutes",
      "max by (node_prefix) (max_over_time(hybrid_clock_skew"
          + "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 "
          + "{{ query_condition }} {{ query_threshold }}",
      "Max clock skew for universe '{{ $labels.target_name }}'"
          + " is above {{ $labels.threshold }} ms."
          + " Current value is {{ $value | printf \\\"%.0f\\\" }} ms",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          AlertDefinitionGroup.Severity.SEVERE,
          DefaultThreshold.from("yb.alert.max_clock_skew_ms")),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.GREATER_THAN,
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
      "Average memory usage for universe '{{ $labels.target_name }}'"
          + " is above {{ $labels.threshold }}%."
          + " Current value is {{ $value | printf \\\"%.0f\\\" }}%",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          AlertDefinitionGroup.Severity.SEVERE,
          DefaultThreshold.from("yb.alert.max_memory_cons_pct")),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.GREATER_THAN,
      Unit.PERCENT),

  HEALTH_CHECK_ERROR(
      "Health Check Error",
      "Failed to perform health check",
      "ybp_health_check_status{universe_uuid = \"__universeUuid__\"} {{ query_condition }} 1",
      "Failed to perform health check for universe '{{ $labels.target_name }}': "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.LESS_THAN,
      Unit.STATUS),

  HEALTH_CHECK_NODE_ERRORS(
      "Health Check Node Errors",
      "Number of nodes with health check errors is above threshold",
      "ybp_health_check_nodes_with_errors{universe_uuid = \"__universeUuid__\"}"
          + " {{ query_condition }} {{ query_threshold }}",
      "Health check for universe '{{ $labels.target_name }}' has errors for"
          + " {{ $value | printf \\\"%.0f\\\" }} node(s)",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          AlertDefinitionGroup.Severity.SEVERE,
          DefaultThreshold.from("yb.alert.health_check_nodes")),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.GREATER_THAN,
      Unit.COUNT),

  HEALTH_CHECK_NOTIFICATION_ERROR(
      "Health Check Notification Error",
      "Failed to perform health check notification",
      "ybp_health_check_notification_status{universe_uuid = \"__universeUuid__\"}"
          + " {{ query_condition }} 1",
      "Failed to perform health check notification for universe '{{ $labels.target_name }}': "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.LESS_THAN,
      Unit.STATUS),

  BACKUP_FAILURE(
      "Backup Failure",
      "Last universe backup creation task failed",
      "ybp_create_backup_status{universe_uuid = \"__universeUuid__\"}" + " {{ query_condition }} 1",
      "Last backup task for universe '{{ $labels.target_name }}' failed: "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.LESS_THAN,
      Unit.STATUS),

  INACTIVE_CRON_NODES(
      "Inactive Cronjob Nodes",
      "Number of nodes with inactive cronjob is above threshold",
      "ybp_universe_inactive_cron_nodes{universe_uuid = \"__universeUuid__\"}"
          + " {{ query_condition }} {{ query_threshold }}",
      "{{ $value | printf \\\"%.0f\\\" }} nodes has inactive cronjob"
          + " for universe '{{ $labels.target_name }}'.",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(
          AlertDefinitionGroup.Severity.SEVERE,
          DefaultThreshold.from("yb.alert.inactive_cronjob_nodes")),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.GREATER_THAN,
      Unit.COUNT),

  ALERT_QUERY_FAILED(
      "Alert Query Failed",
      "Failed to query alerts from Prometheus",
      "ybp_alert_query_status {{ query_condition }} 1",
      "Last alert query for customer '{{ $labels.target_name }}' failed: "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertDefinitionGroup.TargetType.CUSTOMER,
      AlertDefinitionGroupThreshold.Condition.LESS_THAN,
      Unit.COUNT),

  ALERT_CONFIG_WRITING_FAILED(
      "Alert Rules Sync Failed",
      "Failed to sync alerting rules to Prometheus",
      "ybp_alert_config_writer_status {{ query_condition }} 1",
      "Last alert rules sync for customer '{{ $labels.target_name }}' failed: "
          + " {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertDefinitionGroup.TargetType.CUSTOMER,
      AlertDefinitionGroupThreshold.Condition.LESS_THAN,
      Unit.COUNT),

  ALERT_NOTIFICATION_ERROR(
      "Alert Notification Failed",
      "Failed to send alert notifications",
      "ybp_alert_manager_status{customer_uuid = \"__customerUuid__\"}" + " {{ query_condition }} 1",
      "Last attempt to send alert notifications for customer '{{ $labels.target_name }}'"
          + " failed: {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertDefinitionGroup.TargetType.CUSTOMER,
      AlertDefinitionGroupThreshold.Condition.LESS_THAN,
      Unit.COUNT),

  ALERT_NOTIFICATION_CHANNEL_ERROR(
      "Alert Channel Failed",
      "Failed to send alerts to notification channel",
      "ybp_alert_manager_receiver_status{customer_uuid = \"__customerUuid__\"}"
          + " {{ query_condition }} 1",
      "Last attempt to send alert notifications to channel '{{ $labels.target_name }}'"
          + " failed: {{ $labels.error_message }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER, DefinitionSettings.SKIP_TARGET_LABELS),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, DefaultThreshold.statusOk()),
      AlertDefinitionGroup.TargetType.CUSTOMER,
      AlertDefinitionGroupThreshold.Condition.LESS_THAN,
      Unit.COUNT);

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

  private final Map<AlertDefinitionGroup.Severity, DefaultThreshold> defaultThresholdMap;

  private final AlertDefinitionGroup.TargetType targetType;

  private final AlertDefinitionGroupThreshold.Condition defaultThresholdCondition;

  private final Unit defaultThresholdUnit;

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

  AlertDefinitionTemplate(
      String name,
      String description,
      String queryTemplate,
      String summaryTemplate,
      int defaultDurationSec,
      EnumSet<DefinitionSettings> settings,
      Map<AlertDefinitionGroup.Severity, DefaultThreshold> defaultThresholdParamMap,
      AlertDefinitionGroup.TargetType targetType,
      AlertDefinitionGroupThreshold.Condition defaultThresholdCondition,
      Unit defaultThresholdUnit) {
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
