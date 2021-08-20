// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.util.UUID;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Value;

@Value
@Builder
@EqualsAndHashCode
public class MetricTargetKey {
  UUID customerUuid;
  String name;
  UUID targetUuid;

  public static MetricTargetKey from(Metric metric) {
    return MetricTargetKey.builder()
        .customerUuid(metric.getCustomerUUID())
        .name(metric.getName())
        .targetUuid(metric.getTargetUuid())
        .build();
  }
}
