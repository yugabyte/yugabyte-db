package com.yugabyte.yw.models.helpers.telemetry;

public enum ProviderType {
  DATA_DOG(true, true),
  SPLUNK(true, false),
  AWS_CLOUDWATCH(true, false),
  GCP_CLOUD_MONITORING(true, false),
  LOKI(true, false),
  DYNATRACE(false, true),
  S3(true, false),
  OTLP(true, true);

  public final boolean isAllowedForLogs;
  public final boolean isAllowedForMetrics;

  private ProviderType(boolean isAllowedForLogs, boolean isAllowedForMetrics) {
    this.isAllowedForLogs = isAllowedForLogs;
    this.isAllowedForMetrics = isAllowedForMetrics;
  }
}
