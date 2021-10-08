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

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import akka.dispatch.Dispatcher;
import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import scala.concurrent.ExecutionContext;

@RunWith(MockitoJUnitRunner.Silent.class)
public class AlertConfigurationWriterTest extends FakeDBApplication {

  @Mock private ExecutionContext executionContext;

  @Mock private ActorSystem actorSystem;

  @Mock private SwamperHelper swamperHelper;

  @Mock private MetricQueryHelper queryHelper;

  @Mock private RuntimeConfigFactory configFactory;

  private AlertConfigurationWriter configurationWriter;

  private Customer customer;

  private Universe universe;

  @Mock private Config globalConfig;

  private AlertConfiguration configuration;

  private AlertDefinition definition;

  @Before
  public void setUp() {
    when(actorSystem.scheduler()).thenReturn(mock(Scheduler.class));
    when(globalConfig.getInt(AlertConfigurationWriter.CONFIG_SYNC_INTERVAL_PARAM)).thenReturn(1);
    when(configFactory.globalRuntimeConf()).thenReturn(globalConfig);
    when(actorSystem.dispatcher()).thenReturn(mock(Dispatcher.class));
    configurationWriter =
        new AlertConfigurationWriter(
            executionContext,
            actorSystem,
            metricService,
            alertDefinitionService,
            alertConfigurationService,
            swamperHelper,
            queryHelper,
            configFactory);

    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getCustomerId());

    configuration = ModelFactory.createAlertConfiguration(customer, universe);
    definition = ModelFactory.createAlertDefinition(customer, universe, configuration);
  }

  @Test
  public void testSyncActiveDefinition() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    configurationWriter.syncDefinitions();

    AlertDefinition expected = alertDefinitionService.get(definition.getUuid());

    verify(swamperHelper, times(1)).writeAlertDefinition(configuration, expected);
    verify(queryHelper, times(1)).postManagementCommand("reload");

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .name(PlatformMetrics.ALERT_CONFIG_WRITER_STATUS.getMetricName())
            .build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_CONFIG_WRITTEN.getMetricName()).build(),
        1.0);
  }

  @Test
  public void testSyncNotActiveDefinition() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    configuration.setActive(false);
    alertConfigurationService.save(configuration);
    definition = alertDefinitionService.save(definition);

    configurationWriter.syncDefinitions();

    verify(swamperHelper, times(1)).removeAlertDefinition(definition.getUuid());
    verify(queryHelper, times(1)).postManagementCommand("reload");

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .name(PlatformMetrics.ALERT_CONFIG_WRITER_STATUS.getMetricName())
            .build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_CONFIG_WRITTEN.getMetricName()).build(),
        0.0);
  }

  @Test
  public void testSyncExistingAndMissingDefinitions() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    UUID missingDefinitionUuid = UUID.randomUUID();
    when(swamperHelper.getAlertDefinitionConfigUuids())
        .thenReturn(ImmutableList.of(missingDefinitionUuid));
    configurationWriter.syncDefinitions();

    AlertDefinition expected = alertDefinitionService.get(definition.getUuid());

    verify(swamperHelper, times(1)).writeAlertDefinition(configuration, expected);
    verify(swamperHelper, times(1)).removeAlertDefinition(missingDefinitionUuid);
    verify(queryHelper, times(1)).postManagementCommand("reload");

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .name(PlatformMetrics.ALERT_CONFIG_WRITER_STATUS.getMetricName())
            .build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_CONFIG_WRITTEN.getMetricName()).build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_CONFIG_REMOVED.getMetricName()).build(),
        1.0);
  }

  @Test
  public void testNothingToSync() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    alertConfigurationService.delete(configuration.getUuid());

    configurationWriter.syncDefinitions();

    verify(swamperHelper, never()).writeAlertDefinition(any(), any());
    verify(swamperHelper, never()).removeAlertDefinition(any());
    // Called once after startup
    verify(queryHelper, times(1)).postManagementCommand("reload");

    configurationWriter.syncDefinitions();

    verify(swamperHelper, never()).writeAlertDefinition(any(), any());
    verify(swamperHelper, never()).removeAlertDefinition(any());
    // Not called on subsequent run
    verify(queryHelper, times(1)).postManagementCommand("reload");

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .name(PlatformMetrics.ALERT_CONFIG_WRITER_STATUS.getMetricName())
            .build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_CONFIG_WRITTEN.getMetricName()).build(),
        0.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_CONFIG_REMOVED.getMetricName()).build(),
        0.0);
  }

  @Test
  public void testPrometheusManagementDisabled() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(false);
    configurationWriter.syncDefinitions();

    AlertDefinition expected = alertDefinitionService.get(definition.getUuid());

    verify(swamperHelper, times(1)).writeAlertDefinition(configuration, expected);
    verify(queryHelper, never()).postManagementCommand("reload");

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .name(PlatformMetrics.ALERT_CONFIG_WRITER_STATUS.getMetricName())
            .build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_CONFIG_WRITTEN.getMetricName()).build(),
        1.0);
  }
}
