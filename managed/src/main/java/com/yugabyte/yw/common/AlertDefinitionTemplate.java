// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertDefinitionGroupThreshold;
import com.yugabyte.yw.models.common.Unit;
import java.util.EnumSet;
import java.util.Map;
import lombok.Getter;

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
      15,
      EnumSet.noneOf(DefinitionSettings.class),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, "yb.alert.replication_lag_ms"),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.GREATER_THAN,
      Unit.MILLISECOND),

  CLOCK_SKEW(
      "Clock Skew",
      "Max universe clock skew in ms is above threshold during last 10 minutes",
      "max by (node_prefix) (max_over_time(hybrid_clock_skew"
          + "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 "
          + "{{ query_condition }} {{ query_threshold }}",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, "yb.alert.max_clock_skew_ms"),
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
          + " {{ query_condition }} {{ query_threshold }} / 100",
      15,
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER),
      ImmutableMap.of(AlertDefinitionGroup.Severity.SEVERE, "yb.alert.max_memory_cons_pct"),
      AlertDefinitionGroup.TargetType.UNIVERSE,
      AlertDefinitionGroupThreshold.Condition.GREATER_THAN,
      Unit.PERCENT);
  // @formatter:on

  enum DefinitionSettings {
    CREATE_FOR_NEW_CUSTOMER
  }

  private final String name;

  private final String description;

  private final String template;

  private final int defaultDurationSec;

  private final EnumSet<DefinitionSettings> settings;

  private final Map<AlertDefinitionGroup.Severity, String> defaultThresholdParamMap;

  private final AlertDefinitionGroup.TargetType targetType;

  private final AlertDefinitionGroupThreshold.Condition defaultThresholdCondition;

  private final Unit defaultThresholdUnit;

  /**
   * Prepares the template for further usage. Does a substitution for parameter '__nodePrefix__'.
   *
   * @param nodePrefix
   * @return Built string
   */
  public String buildTemplate(String nodePrefix) {
    return template.replaceAll("__nodePrefix__", nodePrefix);
  }

  AlertDefinitionTemplate(
      String name,
      String description,
      String template,
      int defaultDurationSec,
      EnumSet<DefinitionSettings> settings,
      Map<AlertDefinitionGroup.Severity, String> defaultThresholdParamMap,
      AlertDefinitionGroup.TargetType targetType,
      AlertDefinitionGroupThreshold.Condition defaultThresholdCondition,
      Unit defaultThresholdUnit) {
    this.name = name;
    this.description = description;
    this.template = template;
    this.defaultDurationSec = defaultDurationSec;
    this.settings = settings;
    this.defaultThresholdParamMap = defaultThresholdParamMap;
    this.targetType = targetType;
    this.defaultThresholdCondition = defaultThresholdCondition;
    this.defaultThresholdUnit = defaultThresholdUnit;
  }

  public boolean isCreateForNewCustomer() {
    return settings.contains(DefinitionSettings.CREATE_FOR_NEW_CUSTOMER);
  }
}
