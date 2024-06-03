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

import static com.yugabyte.yw.common.TestUtils.replaceFirstChar;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.temporal.ChronoUnit;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.Silent.class)
public class AlertConfigurationWriterTest extends FakeDBApplication {

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock private SwamperHelper swamperHelper;

  @Mock private MetricQueryHelper queryHelper;

  @Mock private RuntimeConfigFactory configFactory;

  private AlertConfigurationWriter configurationWriter;

  private Customer customer;

  private Universe universe;

  @Mock private Config globalConfig;

  private AlertConfiguration configuration;

  private AlertDefinition definition;

  MaintenanceService maintenanceService;

  @Before
  public void setUp() {
    when(globalConfig.getInt(AlertConfigurationWriter.CONFIG_SYNC_INTERVAL_PARAM)).thenReturn(1);
    when(configFactory.globalRuntimeConf()).thenReturn(globalConfig);
    maintenanceService = app.injector().instanceOf(MaintenanceService.class);
    AlertTemplateSettingsService alertTemplateSettingsService =
        app.injector().instanceOf(AlertTemplateSettingsService.class);
    configurationWriter =
        new AlertConfigurationWriter(
            mockPlatformScheduler,
            metricService,
            alertDefinitionService,
            alertConfigurationService,
            alertTemplateSettingsService,
            swamperHelper,
            queryHelper,
            configFactory,
            maintenanceService);

    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());

    configuration = ModelFactory.createAlertConfiguration(customer, universe);
    definition = ModelFactory.createAlertDefinition(customer, universe, configuration);
  }

  @Test
  public void testSyncActiveDefinition() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    configurationWriter.process();

    AlertDefinition expected = alertDefinitionService.get(definition.getUuid());

    verify(swamperHelper, times(1)).writeAlertDefinition(configuration, expected, null);
    verify(swamperHelper, times(1)).writeRecordingRules();
    verify(queryHelper, times(2)).postManagementCommand("reload");

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

    configurationWriter.process();

    verify(swamperHelper, times(1)).removeAlertDefinition(definition.getUuid());
    verify(swamperHelper, times(1)).writeRecordingRules();
    verify(queryHelper, times(2)).postManagementCommand("reload");

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
    configurationWriter.process();

    AlertDefinition expected = alertDefinitionService.get(definition.getUuid());

    verify(swamperHelper, times(1)).writeAlertDefinition(configuration, expected, null);
    verify(swamperHelper, times(1)).removeAlertDefinition(missingDefinitionUuid);
    verify(swamperHelper, times(1)).writeRecordingRules();
    verify(queryHelper, times(2)).postManagementCommand("reload");

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

    configurationWriter.process();

    verify(swamperHelper, never()).writeAlertDefinition(any(), any(), any());
    verify(swamperHelper, never()).removeAlertDefinition(any());
    // Called once after startup
    verify(swamperHelper, times(1)).writeRecordingRules();
    verify(queryHelper, times(2)).postManagementCommand("reload");

    configurationWriter.process();

    verify(swamperHelper, never()).writeAlertDefinition(any(), any(), any());
    verify(swamperHelper, never()).removeAlertDefinition(any());
    // Not called on subsequent run
    verify(swamperHelper, times(1)).writeRecordingRules();
    verify(queryHelper, times(2)).postManagementCommand("reload");

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
    configurationWriter.process();

    AlertDefinition expected = alertDefinitionService.get(definition.getUuid());

    verify(swamperHelper, times(1)).writeAlertDefinition(configuration, expected, null);
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

  @Test
  public void testSyncDefinitionWithMaintenanceWindow() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);

    AlertConfigurationApiFilter filter = new AlertConfigurationApiFilter();
    filter.setTemplate(AlertTemplate.MEMORY_CONSUMPTION);
    MaintenanceWindow maintenanceWindow =
        ModelFactory.createMaintenanceWindow(
            customer.getUuid(),
            window ->
                window
                    .setUuid(replaceFirstChar(window.getUuid(), 'a'))
                    .setAlertConfigurationFilter(filter));
    MaintenanceWindow maintenanceWindow2 =
        ModelFactory.createMaintenanceWindow(
            customer.getUuid(),
            window ->
                window
                    .setUuid(replaceFirstChar(window.getUuid(), 'b'))
                    .setAlertConfigurationFilter(filter));

    configurationWriter.process();

    AlertConfiguration updatedConfiguration =
        alertConfigurationService.get(configuration.getUuid());
    AlertDefinition updatedDefinition = alertDefinitionService.get(definition.getUuid());

    assertThat(
        updatedConfiguration.getMaintenanceWindowUuidsSet(),
        contains(maintenanceWindow.getUuid(), maintenanceWindow2.getUuid()));
    assertThat(
        updatedDefinition.getLabelValue(KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS),
        equalTo(
            maintenanceWindow.getUuid().toString()
                + ","
                + maintenanceWindow2.getUuid().toString()));
    verify(swamperHelper, times(1))
        .writeAlertDefinition(updatedConfiguration, updatedDefinition, null);
    verify(queryHelper, times(2)).postManagementCommand("reload");

    maintenanceWindow.setEndTime(CommonUtils.nowMinusWithoutMillis(1, ChronoUnit.HOURS));
    maintenanceWindow.save();

    configurationWriter.process();

    updatedConfiguration = alertConfigurationService.get(configuration.getUuid());
    updatedDefinition = alertDefinitionService.get(definition.getUuid());

    assertThat(
        updatedConfiguration.getMaintenanceWindowUuidsSet(),
        contains(maintenanceWindow2.getUuid()));
    assertThat(
        updatedDefinition.getLabelValue(KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS),
        equalTo(maintenanceWindow2.getUuid().toString()));
    verify(swamperHelper, times(1))
        .writeAlertDefinition(updatedConfiguration, updatedDefinition, null);
    verify(queryHelper, times(3)).postManagementCommand("reload");

    maintenanceService.delete(maintenanceWindow2.getUuid());

    configurationWriter.process();

    updatedConfiguration = alertConfigurationService.get(configuration.getUuid());
    updatedDefinition = alertDefinitionService.get(definition.getUuid());

    assertThat(updatedConfiguration.getMaintenanceWindowUuids(), nullValue());
    assertThat(
        updatedDefinition.getLabelValue(KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS), nullValue());
    verify(swamperHelper, times(1))
        .writeAlertDefinition(updatedConfiguration, updatedDefinition, null);
    verify(queryHelper, times(4)).postManagementCommand("reload");
  }
}
