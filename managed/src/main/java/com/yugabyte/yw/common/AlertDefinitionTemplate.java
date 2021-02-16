// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import java.util.EnumSet;

public enum AlertDefinitionTemplate {

  REPLICATION_LAG("Replication Lag Alert",
      "max by (node_prefix) (avg_over_time(async_replication_committed_lag_micros" +
      "{node_prefix=\"__nodePrefix__\"}[10m]) or avg_over_time(async_replication_sent_lag_micros" +
      "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 > {{ yb.alert.replication_lag_ms }}",
      EnumSet.noneOf(DefinitionSettings.class), "yb.alert.replication_lag_ms"),

  CLOCK_SKEW("Clock Skew Alert",
      "max by (node_prefix) (max_over_time(hybrid_clock_skew" +
      "{node_prefix=\"__nodePrefix__\"}[10m])) / 1000 > {{ yb.alert.max_clock_skew_ms }}",
      EnumSet.of(DefinitionSettings.CREATE_FOR_NEW_UNIVERSE), "yb.alert.max_clock_skew_ms");

  public enum DefinitionSettings {
    CREATE_FOR_NEW_UNIVERSE
  }

  private final String name;

  private final String template;

  private final EnumSet<DefinitionSettings> settings;

  private final String paramName;

  /**
   * Prepares the template for further usage. Does a substitution for parameter
   * '__nodePrefix__'.
   *
   * @param nodePrefix
   * @return Built string
   */
  public String buildTemplate(String nodePrefix) {
    return template.replaceAll("__nodePrefix__", nodePrefix);
  }

  AlertDefinitionTemplate(String name, String template, EnumSet<DefinitionSettings> settings,
      String paramName) {
    this.name = name;
    this.template = template;
    this.settings = settings;
    this.paramName = paramName;
  }

  public boolean isCreateForNewUniverse() {
    return settings.contains(DefinitionSettings.CREATE_FOR_NEW_UNIVERSE);
  }

  public String getName() {
    return name;
  }

  public String getParameterName() {
    return paramName;
  }
}
