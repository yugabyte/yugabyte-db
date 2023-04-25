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

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class PlatformMetricsProcessor {

  private static final int YB_PROCESS_METRICS_INTERVAL_MIN = 1;

  private static final int YB_CLEAN_CONFIGS_INTERVAL_HOUR = 1;

  private final PlatformScheduler platformScheduler;

  private final MetricService metricService;

  private final SwamperHelper swamperHelper;

  private final List<MetricsProvider> metricsProviderList = new ArrayList<>();

  @Inject
  public PlatformMetricsProcessor(
      PlatformScheduler platformScheduler,
      MetricService metricService,
      UniverseMetricProvider universeMetricProvider,
      SwamperHelper swamperHelper) {
    this.platformScheduler = platformScheduler;
    this.metricService = metricService;
    this.swamperHelper = swamperHelper;
    this.metricsProviderList.add(universeMetricProvider);
  }

  public void start() {
    platformScheduler.schedule(
        "yb-process-metrics",
        Duration.ZERO,
        Duration.ofMinutes(YB_PROCESS_METRICS_INTERVAL_MIN),
        this::scheduleRunner);
    platformScheduler.schedule(
        "yb-clean-configs",
        Duration.ZERO,
        Duration.ofMinutes(YB_CLEAN_CONFIGS_INTERVAL_HOUR),
        this::scheduleCleanup);
  }

  @VisibleForTesting
  void scheduleRunner() {
    try {
      updateMetrics();
      cleanExpiredMetrics();
    } catch (Exception e) {
      log.error("Error processing metrics", e);
    } finally {
      metricService.setFailureStatusMetric(
          buildMetricTemplate(PlatformMetrics.METRIC_PROCESSOR_STATUS));
    }
  }

  @VisibleForTesting
  void scheduleCleanup() {
    try {
      cleanOrphanedSwamperTargets();
    } catch (Exception e) {
      log.error("Error cleaning swamper targets", e);
    }
  }

  private void updateMetrics() {
    for (MetricsProvider provider : metricsProviderList) {
      try {
        provider
            .getMetricGroups()
            .forEach(
                group ->
                    metricService.cleanAndSave(group.getMetrics(), group.getCleanMetricFilter()));
      } catch (Exception e) {
        log.error("Failed to get platform metrics from provider " + provider.getName(), e);
      }
    }
  }

  private void cleanExpiredMetrics() {
    MetricFilter metricFilter = MetricFilter.builder().expired(true).build();
    metricService.delete(metricFilter);
  }

  private void cleanOrphanedSwamperTargets() {
    Set<UUID> existingUniverseUuids = Universe.getAllUUIDs();
    List<UUID> targetFileUuids = swamperHelper.getTargetUniverseUuids();
    targetFileUuids.stream()
        .filter(uuid -> !existingUniverseUuids.contains(uuid))
        .forEach(swamperHelper::removeUniverseTargetJson);
  }
}
