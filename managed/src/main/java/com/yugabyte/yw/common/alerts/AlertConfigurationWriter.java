/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.alerts;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
@Slf4j
public class AlertConfigurationWriter {

  private static final int MIN_CONFIG_SYNC_INTERVAL_SEC = 15;

  @VisibleForTesting
  static final String CONFIG_SYNC_INTERVAL_PARAM = "yb.alert.config_sync_interval_sec";

  private final AtomicBoolean running = new AtomicBoolean(false);
  private final AtomicBoolean requiresReload = new AtomicBoolean(true);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final MetricService metricService;

  private final AlertDefinitionService alertDefinitionService;

  private final AlertConfigurationService alertConfigurationService;

  private final SwamperHelper swamperHelper;

  private final MetricQueryHelper metricQueryHelper;

  private final RuntimeConfigFactory configFactory;

  @Inject
  public AlertConfigurationWriter(
      ExecutionContext executionContext,
      ActorSystem actorSystem,
      MetricService metricService,
      AlertDefinitionService alertDefinitionService,
      AlertConfigurationService alertConfigurationService,
      SwamperHelper swamperHelper,
      MetricQueryHelper metricQueryHelper,
      RuntimeConfigFactory configFactory) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.metricService = metricService;
    this.alertDefinitionService = alertDefinitionService;
    this.alertConfigurationService = alertConfigurationService;
    this.swamperHelper = swamperHelper;
    this.metricQueryHelper = metricQueryHelper;
    this.configFactory = configFactory;
    this.initialize();
  }

  private void initialize() {
    int configSyncPeriodSec = configFactory.globalRuntimeConf().getInt(CONFIG_SYNC_INTERVAL_PARAM);
    if (configSyncPeriodSec < MIN_CONFIG_SYNC_INTERVAL_SEC) {
      log.warn(
          "Alert config sync interval in runtime config is set to {},"
              + " which less than {} seconds. Using minimal value",
          configSyncPeriodSec,
          MIN_CONFIG_SYNC_INTERVAL_SEC);
      configSyncPeriodSec = MIN_CONFIG_SYNC_INTERVAL_SEC;
    }
    this.actorSystem
        .scheduler()
        .schedule(
            Duration.Zero(),
            Duration.create(configSyncPeriodSec, TimeUnit.SECONDS),
            this::syncDefinitions,
            this.executionContext);
  }

  public void scheduleDefinitionSync(UUID definitionUuid) {
    this.actorSystem.dispatcher().execute(() -> syncDefinition(definitionUuid));
  }

  private SyncResult syncDefinition(UUID definitionUuid) {
    try {
      AlertDefinition definition = alertDefinitionService.get(definitionUuid);
      AlertConfiguration configuration =
          definition != null
              ? alertConfigurationService.get(definition.getConfigurationUUID())
              : null;
      if (definition == null || configuration == null || !configuration.isActive()) {
        swamperHelper.removeAlertDefinition(definitionUuid);
        requiresReload.set(true);
        return SyncResult.REMOVED;
      }
      if (definition.isConfigWritten()) {
        log.info("Alert definition {} has config in sync", definitionUuid);
        return SyncResult.IN_SYNC;
      }
      swamperHelper.writeAlertDefinition(configuration, definition);
      definition.setConfigWritten(true);
      alertDefinitionService.save(definition);
      requiresReload.set(true);
      return SyncResult.SYNCED;
    } catch (Exception e) {
      log.error("Error syncing alert definition " + definitionUuid + " config", e);
      return SyncResult.FAILURE;
    }
  }

  @VisibleForTesting
  void syncDefinitions() {
    if (!running.compareAndSet(false, true)) {
      log.info("Previous run of alert configuration writer is still underway");
      return;
    }
    try {
      AlertDefinitionFilter filter = AlertDefinitionFilter.builder().configWritten(false).build();
      List<SyncResult> results = new ArrayList<>();
      alertDefinitionService.process(
          filter, definition -> results.add(syncDefinition(definition.getUuid())));

      List<UUID> configUuids = swamperHelper.getAlertDefinitionConfigUuids();
      Set<UUID> definitionUuids =
          new HashSet<>(alertDefinitionService.listIds(AlertDefinitionFilter.builder().build()));

      results.addAll(
          configUuids
              .stream()
              .filter(uuid -> !definitionUuids.contains(uuid))
              .map(this::syncDefinition)
              .collect(Collectors.toList()));

      metricService.setMetric(
          buildMetricTemplate(PlatformMetrics.ALERT_CONFIG_SYNC_FAILED),
          results.stream().filter(result -> result == SyncResult.FAILURE).count());
      metricService.setMetric(
          buildMetricTemplate(PlatformMetrics.ALERT_CONFIG_WRITTEN),
          results.stream().filter(result -> result == SyncResult.SYNCED).count());
      metricService.setMetric(
          buildMetricTemplate(PlatformMetrics.ALERT_CONFIG_REMOVED),
          results.stream().filter(result -> result == SyncResult.REMOVED).count());
      if (requiresReload.get()) {
        if (metricQueryHelper.isPrometheusManagementEnabled()) {
          metricQueryHelper.postManagementCommand(MetricQueryHelper.MANAGEMENT_COMMAND_RELOAD);
        }
        requiresReload.compareAndSet(true, false);
      }

      metricService.setOkStatusMetric(
          buildMetricTemplate(PlatformMetrics.ALERT_CONFIG_WRITER_STATUS));
    } catch (Exception e) {
      metricService.setStatusMetric(
          buildMetricTemplate(PlatformMetrics.ALERT_CONFIG_WRITER_STATUS),
          "Error syncing alert definition configs " + e.getMessage());
      log.error("Error syncing alert definition configs", e);
    } finally {
      running.set(false);
    }
  }

  private enum SyncResult {
    IN_SYNC,
    SYNCED,
    REMOVED,
    FAILURE
  }
}
