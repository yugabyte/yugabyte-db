// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.util.HashMap;
import java.util.Map;
import java.util.Map.Entry;
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
  Map<String, String> sourceLabels;

  public static class MetricKeyBuilder {
    private UUID customerUuid;
    private String name;
    private UUID sourceUuid;
    Map<String, String> sourceLabels = new HashMap<>();

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

    public MetricKeyBuilder sourceLabels(Map<String, String> sourceLabels) {
      this.sourceLabels = sourceLabels;
      return this;
    }

    public MetricKeyBuilder sourceLabel(String name, String value) {
      this.sourceLabels.put(name, value);
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
            metric
                .getLabels()
                .entrySet()
                .stream()
                .filter(e -> metric.getKeyLabels().contains(e.getKey()))
                .collect(Collectors.toMap(Entry::getKey, Entry::getValue)))
        .build();
  }
}
