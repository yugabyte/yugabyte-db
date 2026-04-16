package com.yugabyte.troubleshoot.ts.metric.models;

import java.time.Duration;
import java.time.ZonedDateTime;
import lombok.Data;
import lombok.EqualsAndHashCode;

@EqualsAndHashCode(callSuper = true)
@Data
public class MetricRangeQuery extends MetricQueryBase {
  private ZonedDateTime start;
  private ZonedDateTime end;
  private Duration step;
}
