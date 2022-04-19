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

  @ApiModelProperty(
      value = "Way of metrics aggregation over time and across nodes",
      required = true)
  MetricAggregation aggregation;

  public static MetricSettings defaultSettings(String metricName) {
    return new MetricSettings().setMetric(metricName).setAggregation(MetricAggregation.DEFAULT);
  }

  public static List<MetricSettings> defaultSettings(Collection<String> metricNames) {
    return metricNames.stream().map(MetricSettings::defaultSettings).collect(Collectors.toList());
  }

  public MetricSettings cloneWithName(String metricName) {
    return new MetricSettings().setMetric(metricName).setAggregation(aggregation);
  }
}
