// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Value;

@Value
@Builder
@EqualsAndHashCode
public class MetricKey {
  MetricTargetKey targetKey;
  String targetLabels;

  public static class MetricKeyBuilder {
    private UUID customerUuid;
    private String name;
    private UUID targetUuid;

    public MetricKeyBuilder customerUuid(UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public MetricKeyBuilder name(String name) {
      this.name = name;
      return this;
    }

    public MetricKeyBuilder targetUuid(UUID targetUuid) {
      this.targetUuid = targetUuid;
      return this;
    }

    public MetricKey build() {
      MetricTargetKey targetKey = this.targetKey;
      if (targetKey == null) {
        targetKey =
            MetricTargetKey.builder()
                .customerUuid(customerUuid)
                .name(name)
                .targetUuid(targetUuid)
                .build();
      }
      return new MetricKey(targetKey, targetLabels);
    }
  }

  public static MetricKey from(Metric metric) {
    return MetricKey.builder()
        .targetKey(MetricTargetKey.from(metric))
        .targetLabels(
            Metric.getTargetLabelsStr(
                metric
                    .getLabels()
                    .stream()
                    .filter(MetricLabel::isTargetLabel)
                    .collect(Collectors.toList())))
        .build();
  }
}
