// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import java.util.EnumSet;

public enum AlertDefinitionTemplate {

  // @formatter:off
  REPLICATION_LAG(
      "Replication Lag Alert",
      "max by (node_prefix) (avg_over_time(async_replication_committed_lag_micros"
          + "{node_prefix=\"__nodePrefix__\"}[10m]) or avg_over_time(async_replication_sent_lag_micros"
          + "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 > {{ query_threshold }}",
      EnumSet.noneOf(DefinitionSettings.class),
      "yb.alert.replication_lag_ms"),

  CLOCK_SKEW(
      "Clock Skew Alert",
      "max by (node_prefix) (max_over_time(hybrid_clock_skew"
          + "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 > {{ query_threshold }}",
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_UNIVERSE),
      "yb.alert.max_clock_skew_ms"),

  MEMORY_CONSUMPTION(
      "Memory Consumption Alert",
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
          + " > {{ query_threshold }} / 100",
      EnumSet.of(
          DefinitionSettings.CREATE_FOR_NEW_UNIVERSE, DefinitionSettings.CREATE_ON_MIGRATION),
      "yb.alert.max_memory_cons_pct");
  // @formatter:on

  enum DefinitionSettings {
    CREATE_FOR_NEW_UNIVERSE,
    CREATE_ON_MIGRATION
  }

  private final String name;

  private final String template;

  private final EnumSet<DefinitionSettings> settings;

  private final String defaultThresholdParamName;

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
      String template,
      EnumSet<DefinitionSettings> settings,
      String defaultThresholdParamName) {
    this.name = name;
    this.template = template;
    this.settings = settings;
    this.defaultThresholdParamName = defaultThresholdParamName;
  }

  public boolean isCreateForNewUniverse() {
    return settings.contains(DefinitionSettings.CREATE_FOR_NEW_UNIVERSE);
  }

  public boolean isCreateOnMigration() {
    return settings.contains(DefinitionSettings.CREATE_ON_MIGRATION);
  }

  public String getName() {
    return name;
  }

  public String getDefaultThresholdParamName() {
    return defaultThresholdParamName;
  }
}
