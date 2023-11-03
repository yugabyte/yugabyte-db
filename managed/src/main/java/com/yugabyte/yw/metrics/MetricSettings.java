/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.metrics;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collection;
import java.util.List;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@ApiModel(description = "Settings, selected for particular metric")
@Accessors(chain = true)
public class MetricSettings {
  @ApiModelProperty(value = "Metric name", required = true)
  String metric;

  @ApiModelProperty(value = "Way of metrics aggregation across nodes")
  NodeAggregation nodeAggregation = NodeAggregation.DEFAULT;

  @ApiModelProperty(value = "Way of metrics aggregation over time")
  TimeAggregation timeAggregation = TimeAggregation.DEFAULT;

  @ApiModelProperty(
      value =
          "Controls if we split nodes, tables, etc. into own lines OR aggregate "
              + " and how we select lines in case of split query")
  SplitMode splitMode = SplitMode.NONE;

  @ApiModelProperty(value = "Defines set of labels, which we use for a split query")
  SplitType splitType = SplitType.NONE;

  @ApiModelProperty(
      value = "Defines how many node lines we return in case we split by nodes/tables/etc.")
  int splitCount;

  @ApiModelProperty(
      value = "Defines if we return 'mean' line with node lines in case we split nodes")
  boolean returnAggregatedValue = true;

  public static MetricSettings defaultSettings(String metricName) {
    return new MetricSettings()
        .setMetric(metricName)
        .setNodeAggregation(NodeAggregation.DEFAULT)
        .setTimeAggregation(TimeAggregation.DEFAULT);
  }

  public static List<MetricSettings> defaultSettings(Collection<String> metricNames) {
    return metricNames.stream().map(MetricSettings::defaultSettings).collect(Collectors.toList());
  }

  public MetricSettings cloneWithName(String metricName) {
    return new MetricSettings()
        .setMetric(metricName)
        .setNodeAggregation(nodeAggregation)
        .setTimeAggregation(timeAggregation)
        .setSplitType(splitType)
        .setSplitMode(splitMode)
        .setSplitCount(splitCount)
        .setReturnAggregatedValue(returnAggregatedValue);
  }
}
