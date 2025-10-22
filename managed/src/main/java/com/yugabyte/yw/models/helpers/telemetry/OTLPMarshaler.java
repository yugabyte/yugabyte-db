package com.yugabyte.yw.models.helpers.telemetry;

import lombok.Getter;

@Getter
public enum OTLPMarshaler {
  OTLP_JSON("otlp_json", true, true),
  SUMO_IC("sumo_ic", true, false);

  private final String name;
  private final boolean allowedForLogs;
  private final boolean allowedForMetrics;

  OTLPMarshaler(String name, boolean allowedForLogs, boolean allowedForMetrics) {
    this.name = name;
    this.allowedForLogs = allowedForLogs;
    this.allowedForMetrics = allowedForMetrics;
  }

  @Override
  public String toString() {
    return this.name();
  }

  public static OTLPMarshaler fromString(String input) {
    for (OTLPMarshaler name : OTLPMarshaler.values()) {
      if (name.name.equalsIgnoreCase(input)) {
        return name;
      }
    }
    throw new IllegalArgumentException(
        "No enum constant " + OTLPMarshaler.class.getName() + "." + input);
  }
}
