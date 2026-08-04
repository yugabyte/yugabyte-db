package com.yugabyte.troubleshoot.ts.metric.models;

import java.time.ZonedDateTime;
import lombok.Data;
import lombok.EqualsAndHashCode;

@EqualsAndHashCode(callSuper = true)
@Data
public class MetricQuery extends MetricQueryBase {
  private ZonedDateTime time;
}
