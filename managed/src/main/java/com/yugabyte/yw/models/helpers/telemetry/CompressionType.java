package com.yugabyte.yw.models.helpers.telemetry;

public enum CompressionType {
  // These are directly substituted to the OtelConfig, cannot be changed
  gzip,
  none,
  snappy,
  zstd;

  @Override
  public String toString() {
    return this.name();
  }

  public static CompressionType fromString(String input) {
    for (CompressionType name : CompressionType.values()) {
      if (name.toString().equalsIgnoreCase(input)) {
        return name;
      }
    }
    throw new IllegalArgumentException(
        "No enum constant " + CompressionType.class.getName() + "." + input);
  }
}
