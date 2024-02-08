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

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
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
  @ApiModelProperty(value = "YbaApi Internal. Metric name", required = true)
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  String metric;

  @ApiModelProperty(value = "YbaApi Internal. Top level aggregation for each metric line.")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  NodeAggregation nodeAggregation = NodeAggregation.DEFAULT;

  @ApiModelProperty(value = "YbaApi Internal. Way of metrics aggregation over time")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  TimeAggregation timeAggregation = TimeAggregation.DEFAULT;

  @ApiModelProperty(
      value =
          "YbaApi Internal. Controls if we split nodes, tables, etc. into own lines OR aggregate "
              + " and how we select lines in case of split query")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  SplitMode splitMode = SplitMode.NONE;

  @ApiModelProperty(
      value = "YbaApi Internal. Defines set of labels, which we use for a " + "split query")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  SplitType splitType = SplitType.NONE;

  @ApiModelProperty(
      value =
          "YbaApi Internal. Defines how many node lines we return in case we split by "
              + "nodes/tables/etc.")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  int splitCount;

  @ApiModelProperty(
      value =
          "YbaApi Internal. Defines if we return additional aggregate time series "
              + "(ex. avg, min) when we are selecting a subset of time series to return "
              + "(ex. top K, bottom k).")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  boolean returnAggregatedValue = true;

  @ApiModelProperty(
      value =
          "YbaApi Internal. Defines the method of metrics aggregation used to obtain additional "
              + "aggregate time series. The provided aggregator wraps over the final query. "
              + "Only applicable if `returnAggregatedValue = true`.")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.1.0")
  NodeAggregation aggregatedValueFunction = NodeAggregation.AVG;

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
        .setAggregatedValueFunction(aggregatedValueFunction)
        .setSplitType(splitType)
        .setSplitMode(splitMode)
        .setSplitCount(splitCount)
        .setReturnAggregatedValue(returnAggregatedValue);
  }
}
