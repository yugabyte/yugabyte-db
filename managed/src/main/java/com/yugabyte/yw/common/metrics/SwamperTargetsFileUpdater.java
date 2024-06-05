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
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import java.time.Duration;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

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

  static final String CLOUD_ENABLED = "yb.cloud.enabled";

  private final PlatformScheduler platformScheduler;
  private final SwamperHelper swamperHelper;
  private final RuntimeConfigFactory configFactory;
  private final MetricService metricService;

  @Inject
  public SwamperTargetsFileUpdater(
      PlatformScheduler platformScheduler,
      SwamperHelper swamperHelper,
      RuntimeConfigFactory configFactory,
      MetricService metricService) {
    this.platformScheduler = platformScheduler;
    this.swamperHelper = swamperHelper;
    this.configFactory = configFactory;
    this.metricService = metricService;
  }

  public void start() {
    boolean isCloudEnabled = configFactory.globalRuntimeConf().getBoolean(CLOUD_ENABLED);
    int configSyncPeriodSec = configFactory.globalRuntimeConf().getInt(CONFIG_SYNC_INTERVAL_PARAM);
    if (configSyncPeriodSec < MIN_CONFIG_SYNC_INTERVAL_SEC) {
      log.warn(
          "Metric target file config sync interval in runtime config is set to {},"
              + " which less than {} seconds. Using minimal value",
          configSyncPeriodSec,
          MIN_CONFIG_SYNC_INTERVAL_SEC);
      configSyncPeriodSec = MIN_CONFIG_SYNC_INTERVAL_SEC;
    }
    if (!isCloudEnabled) {
      platformScheduler.schedule(
          getClass().getSimpleName(),
          Duration.ZERO,
          Duration.ofSeconds(configSyncPeriodSec),
          this::process);
    }
  }

  private void syncUniverse(Universe universe) {
    try {
      swamperHelper.writeUniverseTargetJson(universe);
      universe.setSwamperConfigWritten(true);
      universe.save();
      SWAMPER_TARGET_FILE_UPDATED_UNIVERSES_COUNTER.inc();
    } catch (Exception e) {
      log.error("Error syncing swamper target files for universe " + universe.getUniverseUUID(), e);
      SWAMPER_TARGET_FILE_FAILED_UNIVERSES_COUNTER.inc();
    }
  }

  @VisibleForTesting
  void process() {
    syncUniverses();
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
