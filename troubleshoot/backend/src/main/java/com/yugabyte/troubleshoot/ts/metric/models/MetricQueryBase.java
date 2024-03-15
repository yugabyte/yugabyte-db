package com.yugabyte.troubleshoot.ts.metric.models;

import java.time.Duration;
import lombok.Data;

@Data
public class MetricQueryBase {
  private String query;
  private Duration timeout;
}
