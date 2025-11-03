package com.yugabyte.yw.models.helpers.telemetry;

public enum CompressionType {
  gzip("gzip"),
  none("none"),
  snappy("snappy"),
  zstd("zstd");
  private final String type;

  CompressionType(String type) {
    this.type = type;
  }

  public String getType() {
    return type;
  }

  @Override
  public String toString() {
    return this.name();
  }

  public static CompressionType fromString(String input) {
    for (CompressionType name : CompressionType.values()) {
      if (name.type.equalsIgnoreCase(input)) {
        return name;
      }
    }
    throw new IllegalArgumentException(
        "No enum constant " + CompressionType.class.getName() + "." + input);
  }
}
