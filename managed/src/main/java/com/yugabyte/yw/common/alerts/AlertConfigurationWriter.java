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

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

@Singleton
public class AlertConfigurationWriter {
  public static final Logger LOG = LoggerFactory.getLogger(AlertConfigurationWriter.class);

  private static final int MIN_CONFIG_SYNC_INTERVAL_SEC = 15;

  @VisibleForTesting
  static final String CONFIG_SYNC_INTERVAL_PARAM = "yb.alert.config_sync_interval_sec";

  private AtomicBoolean running = new AtomicBoolean(false);
  private AtomicBoolean requiresReload = new AtomicBoolean(true);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private final AlertDefinitionService alertDefinitionService;

  private final SwamperHelper swamperHelper;

  private final MetricQueryHelper metricQueryHelper;

  private final RuntimeConfigFactory configFactory;

  @Inject
  public AlertConfigurationWriter(
      ExecutionContext executionContext,
      ActorSystem actorSystem,
      AlertDefinitionService alertDefinitionService,
      SwamperHelper swamperHelper,
      MetricQueryHelper metricQueryHelper,
      RuntimeConfigFactory configFactory) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.alertDefinitionService = alertDefinitionService;
    this.swamperHelper = swamperHelper;
    this.metricQueryHelper = metricQueryHelper;
    this.configFactory = configFactory;
    this.initialize();
  }

  private void initialize() {
    int configSyncPeriodSec = configFactory.globalRuntimeConf().getInt(CONFIG_SYNC_INTERVAL_PARAM);
    if (configSyncPeriodSec < MIN_CONFIG_SYNC_INTERVAL_SEC) {
      LOG.warn(
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
    this.actorSystem
        .dispatcher()
        .execute(
            () -> {
              syncDefinition(definitionUuid);
            });
  }

  private void syncDefinition(UUID definitionUuid) {
    try {
      AlertDefinition definition = alertDefinitionService.get(definitionUuid);
      if (definition == null || !definition.isActive()) {
        swamperHelper.removeAlertDefinition(definitionUuid);
        return;
      }
      if (definition.isConfigWritten()) {
        LOG.info("Alert definition {} has config in sync", definitionUuid);
        return;
      }
      swamperHelper.writeAlertDefinition(definition);
      definition.setConfigWritten(true);
      alertDefinitionService.update(definition);
      requiresReload.set(true);
    } catch (Exception e) {
      LOG.error("Error syncing alert definition " + definitionUuid + " config", e);
    }
  }

  @VisibleForTesting
  void syncDefinitions() {
    if (running.compareAndSet(false, true)) {
      try {
        AlertDefinitionFilter filter = new AlertDefinitionFilter().setConfigWritten(false);
        alertDefinitionService.process(
            filter,
            definition -> {
              syncDefinition(definition.getUuid());
            });

        List<UUID> configUuids = swamperHelper.getAlertDefinitionConfigUuids();
        Set<UUID> definitionUuids =
            new HashSet<>(alertDefinitionService.listIds(new AlertDefinitionFilter()));

        configUuids
            .stream()
            .filter(uuid -> !definitionUuids.contains(uuid))
            .forEach(this::syncDefinition);

        if (requiresReload.get()) {
          metricQueryHelper.postManagementCommand(MetricQueryHelper.MANAGEMENT_COMMAND_RELOAD);
          requiresReload.compareAndSet(true, false);
        }
      } catch (Exception e) {
        LOG.error("Error syncing alert definition configs", e);
      }

      running.set(false);
    }
  }
}
