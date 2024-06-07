// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.util.UUID;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Value;

@Value
@Builder
@EqualsAndHashCode
public class MetricSourceKey {
  UUID customerUuid;
  String name;
  UUID sourceUuid;

  public static MetricSourceKey from(Metric metric) {
    return MetricSourceKey.builder()
        .customerUuid(metric.getCustomerUUID())
        .name(metric.getName())
        .sourceUuid(metric.getSourceUuid())
        .build();
  }
}
