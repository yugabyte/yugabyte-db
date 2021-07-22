// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Value;
import java.util.UUID;

@Value
@Builder
@EqualsAndHashCode
public class MetricKey {
  UUID customerUuid;
  String name;
  UUID targetUuid;

  public static MetricKey from(Metric metric) {
    return MetricKey.builder()
        .customerUuid(metric.getCustomerUUID())
        .name(metric.getName())
        .targetUuid(metric.getTargetUuid())
        .build();
  }
}
