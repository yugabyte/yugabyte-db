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

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.common.collect.ImmutableList;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

@Singleton
public class UniverseMetricProvider implements MetricsProvider {

  private static final List<PlatformMetrics> UNIVERSE_METRICS =
      ImmutableList.of(
          PlatformMetrics.UNIVERSE_EXISTS,
          PlatformMetrics.UNIVERSE_PAUSED,
          PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS,
          PlatformMetrics.UNIVERSE_BACKUP_IN_PROGRESS);

  @Override
  public List<Metric> getMetrics() throws Exception {
    List<Metric> metrics = new ArrayList<>();
    for (Customer customer : Customer.getAll()) {
      for (Universe universe : Universe.getAllWithoutResources(customer)) {
        metrics.add(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_EXISTS, customer, universe)
                .setValue(MetricService.STATUS_OK));
        metrics.add(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_PAUSED, customer, universe)
                .setValue(statusValue(universe.getUniverseDetails().universePaused)));
        metrics.add(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS, customer, universe)
                .setValue(statusValue(universe.getUniverseDetails().updateInProgress)));
        metrics.add(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_BACKUP_IN_PROGRESS, customer, universe)
                .setValue(statusValue(universe.getUniverseDetails().backupInProgress)));
      }
    }
    return metrics;
  }

  @Override
  public List<MetricFilter> getMetricsToRemove() throws Exception {
    return Collections.singletonList(MetricFilter.builder().metrics(UNIVERSE_METRICS).build());
  }

  @Override
  public String getName() {
    return "Universe metrics";
  }
}
