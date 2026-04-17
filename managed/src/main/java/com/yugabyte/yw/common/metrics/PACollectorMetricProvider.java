// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.metrics;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class PACollectorMetricProvider implements MetricsProvider {

  private final PerfAdvisorService perfAdvisorService;

  @Inject
  public PACollectorMetricProvider(PerfAdvisorService perfAdvisorService) {
    this.perfAdvisorService = perfAdvisorService;
  }

  @Override
  public List<MetricSaveGroup> getMetricGroups() {
    List<MetricSaveGroup> metricSaveGroups = new ArrayList<>();

    for (Customer customer : Customer.getAll()) {
      PACollectorFilter filter =
          PACollectorFilter.builder().customerUuid(customer.getUuid()).build();
      List<PACollector> collectors = perfAdvisorService.list(filter);

      for (PACollector collector : collectors) {
        Set<Universe> universes = Universe.getAllWithoutResources(customer);
        boolean hasRegisteredUniverses =
            universes.stream()
                .anyMatch(
                    u ->
                        !u.getUniverseDetails().universePaused
                            && collector
                                .getUuid()
                                .equals(u.getUniverseDetails().getPaCollectorUuid()));

        MetricSaveGroup group =
            MetricSaveGroup.builder()
                .metric(
                    buildMetricTemplate(PlatformMetrics.PA_COLLECTOR_EXPECTED_UP, customer)
                        // For now, we only allow single collector - so this should be fine
                        // Later we'll need to distinguish between the collectors and
                        // associate collector in the UI with collector metric labels.
                        // We'll have to create swamper target files for collectors and
                        // scrape collector own metrics for alerting with collector UUID as label.
                        .setLabel("collector_id", "local")
                        .setValue(statusValue(hasRegisteredUniverses)))
                .cleanMetricFilter(
                    MetricFilter.builder()
                        .metricNames(
                            Collections.singleton(PlatformMetrics.PA_COLLECTOR_EXPECTED_UP))
                        .sourceUuid(customer.getUuid())
                        .build())
                .build();

        metricSaveGroups.add(group);
      }
    }
    return metricSaveGroups;
  }

  @Override
  public String getName() {
    return "PA Collector metrics";
  }
}
