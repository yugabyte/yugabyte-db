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
  MetricSourceKey sourceKey;
  String sourceLabels;

  public static class MetricKeyBuilder {
    private UUID customerUuid;
    private String name;
    private UUID sourceUuid;

    public MetricKeyBuilder customerUuid(UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public MetricKeyBuilder name(String name) {
      this.name = name;
      return this;
    }

    public MetricKeyBuilder targetUuid(UUID targetUuid) {
      this.sourceUuid = targetUuid;
      return this;
    }

    public MetricKey build() {
      MetricSourceKey sourceKey = this.sourceKey;
      if (sourceKey == null) {
        sourceKey =
            MetricSourceKey.builder()
                .customerUuid(customerUuid)
                .name(name)
                .sourceUuid(sourceUuid)
                .build();
      }
      return new MetricKey(sourceKey, sourceLabels);
    }
  }

  public static MetricKey from(Metric metric) {
    return MetricKey.builder()
        .sourceKey(MetricSourceKey.from(metric))
        .sourceLabels(
            Metric.getSourceLabelsStr(
                metric
                    .getLabels()
                    .stream()
                    .filter(MetricLabel::isSourceLabel)
                    .collect(Collectors.toList())))
        .build();
  }
}
