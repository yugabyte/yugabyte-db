// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.empty;
import static org.hamcrest.Matchers.hasSize;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AlertManager;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.alerts.impl.AlertChannelEmail;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.data.AlertData;
import com.yugabyte.yw.metrics.data.AlertState;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class QueryAlertsTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock private MetricQueryHelper queryHelper;

  @Mock private RuntimeConfigFactory configFactory;

  @Mock private EmailHelper emailHelper;

  @Mock private AlertChannelManager channelsManager;

  @Mock private AlertChannelEmail emailReceiver;

  private QueryAlerts queryAlerts;

  private Customer customer;

  private Universe universe;

  @Mock private Config universeConfig;

  private AlertDefinition definition;

  private AlertChannelService alertChannelService;
  private AlertDestinationService alertDestinationService;
  private AlertManager alertManager;

  @Before
  public void setUp() {

    customer = ModelFactory.testCustomer();

    SmtpData smtpData = new SmtpData();
    when(channelsManager.get(ChannelType.Email.name())).thenReturn(emailReceiver);
    when(emailHelper.getDestinations(customer.getUuid()))
        .thenReturn(Collections.singletonList("to@to.com"));
    when(emailHelper.getSmtpData(customer.getUuid())).thenReturn(smtpData);

    alertChannelService = app.injector().instanceOf(AlertChannelService.class);
    alertDestinationService = app.injector().instanceOf(AlertDestinationService.class);
    alertManager =
        new AlertManager(
            emailHelper,
            alertService,
            alertConfigurationService,
            alertChannelService,
            app.injector().instanceOf(AlertChannelTemplateService.class),
            alertDestinationService,
            channelsManager,
            metricService);
    queryAlerts =
        new QueryAlerts(
            mockPlatformScheduler,
            alertService,
            queryHelper,
            metricService,
            alertDefinitionService,
            alertConfigurationService,
            alertManager);

    universe = ModelFactory.createUniverse(customer.getId());
    when(configFactory.forUniverse(universe)).thenReturn(universeConfig);

    definition = ModelFactory.createAlertDefinition(customer, universe);
  }

  @Test
  public void testQueryAlertsNewAlert() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(ImmutableList.of(createAlertData(raisedTime)));

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert = createAlert(raisedTime).setUuid(alerts.get(0).getUuid());
    copyNotificationFields(expectedAlert, alerts.get(0));
    assertThat(alerts, contains(expectedAlert));

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_STATUS.getMetricName()).build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_TOTAL_ALERTS.getMetricName()).build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_NEW_ALERTS.getMetricName()).build(),
        1.0);
  }

  @Test
  public void testQueryAlertUnderMaintenanceWindow() {
    String maintenanceWindowUuid = UUID.randomUUID().toString();
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts())
        .thenReturn(
            ImmutableList.of(
                createAlertData(
                    raisedTime,
                    labels -> {
                      labels.put(KnownAlertLabels.SEVERITY.labelName(), Severity.SEVERE.name());
                      labels.put(
                          KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS.labelName(),
                          maintenanceWindowUuid);
                    })));

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert =
        createAlert(raisedTime)
            .setUuid(alerts.get(0).getUuid())
            .setState(State.SUSPENDED)
            .setLabel(KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS, maintenanceWindowUuid);
    copyNotificationFields(expectedAlert, alerts.get(0));
    assertThat(alerts, contains(expectedAlert));
  }

  @Test
  public void testQueryAlertsMultipleSeverities() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts())
        .thenReturn(
            ImmutableList.of(
                createAlertData(raisedTime),
                createAlertData(
                    raisedTime,
                    labels ->
                        labels.put(
                            KnownAlertLabels.SEVERITY.labelName(), Severity.WARNING.name()))));

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert = createAlert(raisedTime).setUuid(alerts.get(0).getUuid());
    copyNotificationFields(expectedAlert, alerts.get(0));
    assertThat(alerts, contains(expectedAlert));

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_TOTAL_ALERTS.getMetricName()).build(),
        2.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .name(PlatformMetrics.ALERT_QUERY_FILTERED_ALERTS.getMetricName())
            .build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_NEW_ALERTS.getMetricName()).build(),
        1.0);
  }

  @Test
  public void testQueryAlertsNewAlertWithDefaults() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(ImmutableList.of(createAlertData(raisedTime)));

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    assertThat(alerts, hasSize(1));

    Alert expectedAlert = createAlert(raisedTime).setUuid(alerts.get(0).getUuid());
    copyNotificationFields(expectedAlert, alerts.get(0));
    assertThat(alerts, contains(expectedAlert));
  }

  @Test
  public void testQueryAlertsExistingAlert() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(ImmutableList.of(createAlertData(raisedTime)));

    Alert alert = createAlert(raisedTime);
    alertService.save(alert);

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert =
        createAlert(raisedTime).setUuid(alert.getUuid()).setState(Alert.State.ACTIVE);
    copyNotificationFields(expectedAlert, alerts.get(0));
    assertThat(alerts, contains(expectedAlert));

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_TOTAL_ALERTS.getMetricName()).build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .name(PlatformMetrics.ALERT_QUERY_UPDATED_ALERTS.getMetricName())
            .build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_NEW_ALERTS.getMetricName()).build(),
        0.0);
  }

  private void copyNotificationFields(Alert expectedAlert, Alert alert) {
    expectedAlert
        .setNotificationAttemptTime(alert.getNotificationAttemptTime())
        .setNextNotificationTime(alert.getNextNotificationTime())
        .setNotificationsFailed(alert.getNotificationsFailed());
  }

  @Test
  public void testQueryAlertsExistingResolvedAlert() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(ImmutableList.of(createAlertData(raisedTime)));

    Alert alert = createAlert(raisedTime);
    alert.setState(Alert.State.RESOLVED);
    alertService.save(alert);

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    assertThat(alerts, hasSize(2));

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_TOTAL_ALERTS.getMetricName()).build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_NEW_ALERTS.getMetricName()).build(),
        1.0);
  }

  @Test
  public void testQueryAlertsResolveExistingAlert() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(Collections.emptyList());

    Alert alert = createAlert(raisedTime);
    alertService.save(alert);

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert =
        createAlert(raisedTime)
            .setUuid(alert.getUuid())
            .setState(Alert.State.RESOLVED)
            .setResolvedTime(alerts.get(0).getResolvedTime());
    copyNotificationFields(expectedAlert, alerts.get(0));
    assertThat(alerts, contains(expectedAlert));

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_TOTAL_ALERTS.getMetricName()).build(),
        0.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .name(PlatformMetrics.ALERT_QUERY_RESOLVED_ALERTS.getMetricName())
            .build(),
        1.0);
  }

  @Test
  public void testPrometheusManagementDisabled() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(false);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(ImmutableList.of(createAlertData(raisedTime)));

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    assertThat(alerts, empty());

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_STATUS.getMetricName()).build(),
        1.0);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_TOTAL_ALERTS.getMetricName()).build(),
        null);
    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder().name(PlatformMetrics.ALERT_QUERY_NEW_ALERTS.getMetricName()).build(),
        null);
  }

  @Test
  public void testStandbyInstance() {
    when(queryHelper.isPrometheusManagementEnabled()).thenReturn(true);
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(Collections.emptyList());
    HighAvailabilityConfig config = HighAvailabilityConfig.create("test");
    PlatformInstance follower = PlatformInstance.create(config, "http://ghi.com", false, true);

    Alert alert = createAlert(raisedTime);
    alertService.save(alert);

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter = AlertFilter.builder().customerUuid(customer.getUuid()).build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert =
        createAlert(raisedTime)
            .setUuid(alert.getUuid())
            .setState(Alert.State.RESOLVED)
            .setResolvedTime(alerts.get(0).getResolvedTime());
    copyNotificationFields(expectedAlert, alerts.get(0));
    assertThat(alerts, contains(expectedAlert));
  }

  private Alert createAlert(ZonedDateTime raisedTime) {
    return new Alert()
        .setCreateTime(Date.from(raisedTime.toInstant()))
        .setCustomerUUID(customer.getUuid())
        .setDefinitionUuid(definition.getUuid())
        .setConfigurationUuid(definition.getConfigurationUUID())
        .setConfigurationType(AlertConfiguration.TargetType.UNIVERSE)
        .setSeverity(AlertConfiguration.Severity.SEVERE)
        .setName("Clock Skew Alert")
        .setSourceName("Some Source")
        .setSourceUUID(universe.getUniverseUUID())
        .setMessage("Clock Skew Alert for universe Test is firing")
        .setState(Alert.State.ACTIVE)
        .setLabel(KnownAlertLabels.CUSTOMER_UUID, customer.getUuid().toString())
        .setLabel(KnownAlertLabels.DEFINITION_UUID, definition.getUuid().toString())
        .setLabel(KnownAlertLabels.CONFIGURATION_UUID, definition.getConfigurationUUID().toString())
        .setLabel(
            KnownAlertLabels.CONFIGURATION_TYPE, AlertConfiguration.TargetType.UNIVERSE.name())
        .setLabel(KnownAlertLabels.SOURCE_NAME, "Some Source")
        .setLabel(KnownAlertLabels.SOURCE_UUID, universe.getUniverseUUID().toString())
        .setLabel(KnownAlertLabels.DEFINITION_NAME, "Clock Skew Alert")
        .setLabel(KnownAlertLabels.SEVERITY, AlertConfiguration.Severity.SEVERE.name());
  }

  private AlertData createAlertData(ZonedDateTime raisedTime) {
    return createAlertData(
        raisedTime,
        labels -> labels.put(KnownAlertLabels.SEVERITY.labelName(), Severity.SEVERE.name()));
  }

  private AlertData createAlertData(
      ZonedDateTime raisedTime, Consumer<Map<String, String>> labelMapModifier) {
    Map<String, String> labels = new HashMap<>();
    labels.put("customer_uuid", customer.getUuid().toString());
    labels.put("definition_uuid", definition.getUuid().toString());
    labels.put("configuration_uuid", definition.getConfigurationUUID().toString());
    labels.put("configuration_type", "UNIVERSE");
    labels.put("source_name", "Some Source");
    labels.put("source_uuid", universe.getUniverseUUID().toString());
    labels.put("definition_name", "Clock Skew Alert");
    labelMapModifier.accept(labels);
    return AlertData.builder()
        .activeAt(raisedTime.withZoneSameInstant(ZoneId.of("UTC")))
        .annotations(ImmutableMap.of("summary", "Clock Skew Alert for universe Test is firing"))
        .labels(labels)
        .state(AlertState.firing)
        .value(1)
        .build();
  }
}
