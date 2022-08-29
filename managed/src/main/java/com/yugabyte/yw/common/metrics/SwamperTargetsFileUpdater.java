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

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
@Slf4j
public class SwamperTargetsFileUpdater {

  private static final int MIN_CONFIG_SYNC_INTERVAL_SEC = 15;

  private static final String SWAMPER_TARGET_FILE_UPDATED_UNIVERSES =
      "ybp_swamper_target_file_updated_universes";

  private static final String SWAMPER_TARGET_FILE_FAILED_UNIVERSES =
      "ybp_swamper_target_file_failed_universes";

  private static final Counter SWAMPER_TARGET_FILE_UPDATED_UNIVERSES_COUNTER =
      Counter.build(
              SWAMPER_TARGET_FILE_UPDATED_UNIVERSES,
              "Count of successful updates of swamper target file for universe")
          .register(CollectorRegistry.defaultRegistry);

  private static final Counter SWAMPER_TARGET_FILE_FAILED_UNIVERSES_COUNTER =
      Counter.build(
              SWAMPER_TARGET_FILE_FAILED_UNIVERSES,
              "Count of failed updates of swamper target file for universe")
          .register(CollectorRegistry.defaultRegistry);

  @VisibleForTesting
  static final String CONFIG_SYNC_INTERVAL_PARAM = "yb.metrics.config_sync_interval_sec";

  private final AtomicBoolean running = new AtomicBoolean(false);
  private final ActorSystem actorSystem;
  private final ExecutionContext executionContext;
  private final SwamperHelper swamperHelper;
  private final RuntimeConfigFactory configFactory;
  private final MetricService metricService;

  @Inject
  public SwamperTargetsFileUpdater(
      ActorSystem actorSystem,
      ExecutionContext executionContext,
      SwamperHelper swamperHelper,
      RuntimeConfigFactory configFactory,
      MetricService metricService) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.swamperHelper = swamperHelper;
    this.configFactory = configFactory;
    this.metricService = metricService;
  }

  public void start() {
    int configSyncPeriodSec = configFactory.globalRuntimeConf().getInt(CONFIG_SYNC_INTERVAL_PARAM);
    if (configSyncPeriodSec < MIN_CONFIG_SYNC_INTERVAL_SEC) {
      log.warn(
          "Metric target file config sync interval in runtime config is set to {},"
              + " which less than {} seconds. Using minimal value",
          configSyncPeriodSec,
          MIN_CONFIG_SYNC_INTERVAL_SEC);
      configSyncPeriodSec = MIN_CONFIG_SYNC_INTERVAL_SEC;
    }
    log.info("Scheduling swamper target files updater every " + configSyncPeriodSec + " sec");
    this.actorSystem
        .scheduler()
        .schedule(
            Duration.Zero(), // initialDelay
            Duration.create(configSyncPeriodSec, TimeUnit.SECONDS), // interval
            this::process,
            this.executionContext);
  }

  private void syncUniverse(Universe universe) {
    try {
      swamperHelper.writeUniverseTargetJson(universe);
      universe.updateSwamperConfigWritten(true);
      SWAMPER_TARGET_FILE_UPDATED_UNIVERSES_COUNTER.inc();
    } catch (Exception e) {
      log.error("Error syncing swamper target files for universe " + universe.getUniverseUUID(), e);
      SWAMPER_TARGET_FILE_FAILED_UNIVERSES_COUNTER.inc();
    }
  }

  @VisibleForTesting
  void process() {
    if (!running.compareAndSet(false, true)) {
      log.info("Previous run of swamper target files updater is still underway");
      return;
    }
    try {
      syncUniverses();
    } catch (Exception e) {
      log.error("Error running swamper target files updater", e);
    } finally {
      running.set(false);
    }
  }

  private void syncUniverses() {
    try {
      for (Universe universe : Universe.getUniversesForSwamperConfigUpdate()) {
        syncUniverse(universe);
      }

      metricService.setOkStatusMetric(
          buildMetricTemplate(PlatformMetrics.SWAMPER_FILE_UPDATER_STATUS));
    } catch (Exception e) {
      metricService.setFailureStatusMetric(
          buildMetricTemplate(PlatformMetrics.SWAMPER_FILE_UPDATER_STATUS));
      log.error("Error syncing swamper target files", e);
    }
  }
}
