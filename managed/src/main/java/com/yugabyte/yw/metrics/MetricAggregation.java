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

import java.util.Arrays;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.Getter;
import org.apache.commons.lang3.StringUtils;

@Getter
public enum MetricAggregation {
  DEFAULT(StringUtils.EMPTY, StringUtils.EMPTY),
  MIN("min_over_time", "min"),
  MAX("max_over_time", "max"),
  AVG("avg_over_time", "avg"),
  SUM("avg_over_time", "sum");

  public static final Set<String> TIME_AGGREGATIONS;
  public static final Set<String> NODE_AGGREGATIONS;

  private final String timeAggregationFunc;
  private final String nodeAggregationFunc;

  MetricAggregation(String timeAggregationFunc, String nodeAggregationFunc) {
    this.timeAggregationFunc = timeAggregationFunc;
    this.nodeAggregationFunc = nodeAggregationFunc;
  }

  static {
    TIME_AGGREGATIONS =
        Arrays.stream(MetricAggregation.values())
            .map(MetricAggregation::getTimeAggregationFunc)
            .filter(StringUtils::isNoneEmpty)
            .collect(Collectors.toSet());
    NODE_AGGREGATIONS =
        Arrays.stream(MetricAggregation.values())
            .map(MetricAggregation::getNodeAggregationFunc)
            .filter(StringUtils::isNoneEmpty)
            .collect(Collectors.toSet());
  }
}
