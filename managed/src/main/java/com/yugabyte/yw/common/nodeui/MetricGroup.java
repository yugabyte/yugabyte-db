// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.nodeui;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.Data;

@Data
public class MetricGroup {
  @JsonProperty("type")
  private String type;

  @JsonProperty("id")
  private String id;

  @JsonProperty("metrics")
  private List<Metric> metrics;

  public static class Metric {
    @JsonProperty("name")
    private String name;

    @JsonProperty("value")
    private long value;
  }

  public static Map<String, Long> getTabletFollowerLagMap(List<MetricGroup> MetricGroupList) {
    Map<String, List<MetricGroup.Metric>> idMetricMap =
        MetricGroupList.stream()
            .filter(MetricGroup -> MetricGroup.type.equals("tablet"))
            .collect(
                Collectors.toMap(
                    MetricGroup -> MetricGroup.id, MetricGroup -> MetricGroup.metrics));

    Map<String, Long> tabletFollowerLagMap = new HashMap<>();
    for (Map.Entry<String, List<Metric>> entry : idMetricMap.entrySet()) {
      String id = entry.getKey();
      for (MetricGroup.Metric m : entry.getValue()) {
        if (m.name.equals("follower_lag_ms")) {
          tabletFollowerLagMap.put(id, m.value);
          break;
        }
      }
    }

    return tabletFollowerLagMap;
  }
}
