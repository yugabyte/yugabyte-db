/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.metrics;

import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.filters.MetricFilter;
import java.util.List;

public interface MetricsProvider {
  List<Metric> getMetrics() throws Exception;

  List<MetricFilter> getMetricsToRemove() throws Exception;

  String getName();

  default double statusValue(boolean status) {
    return status ? MetricService.STATUS_OK : MetricService.STATUS_NOT_OK;
  }
}
