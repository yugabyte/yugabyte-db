/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner;

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.alerts.MetricService;
import com.yugabyte.yw.models.filters.MetricFilter;
import lombok.extern.slf4j.Slf4j;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

@Singleton
@Slf4j
public class CleanExpiredMetrics {

  private static final int YB_CLEAN_METRICS_INTERVAL_MIN = 10;

  private AtomicBoolean running = new AtomicBoolean(false);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final MetricService metricService;

  @Inject
  public CleanExpiredMetrics(
      ActorSystem actorSystem, ExecutionContext executionContext, MetricService metricService) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.metricService = metricService;
    this.initialize();
  }

  private void initialize() {
    this.actorSystem
        .scheduler()
        .schedule(
            Duration.Zero(),
            Duration.create(YB_CLEAN_METRICS_INTERVAL_MIN, TimeUnit.MINUTES),
            this::scheduleRunner,
            this.executionContext);
  }

  @VisibleForTesting
  void scheduleRunner() {
    if (running.compareAndSet(false, true)) {
      try {
        MetricFilter metricFilter = MetricFilter.builder().expired(true).build();
        metricService.delete(metricFilter);
      } catch (Exception e) {
        log.error("Error cleaning metrics", e);
      } finally {
        running.set(false);
      }
    }
  }
}
