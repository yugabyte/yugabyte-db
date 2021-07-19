// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.hasSize;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AlertManager;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertReceiverManager;
import com.yugabyte.yw.common.alerts.AlertRouteService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.alerts.impl.AlertReceiverEmail;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.data.AlertData;
import com.yugabyte.yw.metrics.data.AlertState;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import scala.concurrent.ExecutionContext;

@RunWith(JUnitParamsRunner.class)
public class QueryAlertsTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Mock private ExecutionContext executionContext;

  @Mock private ActorSystem actorSystem;

  @Mock private MetricQueryHelper queryHelper;

  @Mock private RuntimeConfigFactory configFactory;

  @Mock private EmailHelper emailHelper;

  @Mock private AlertReceiverManager receiversManager;

  @Mock private AlertReceiverEmail emailReceiver;

  private QueryAlerts queryAlerts;

  private Customer customer;

  private Universe universe;

  @Mock private Config universeConfig;

  private AlertDefinition definition;

  private AlertDefinitionGroupService alertDefinitionGroupService;
  private AlertDefinitionService alertDefinitionService;
  private AlertService alertService;
  private AlertRouteService alertRouteService;
  private AlertManager alertManager;

  @Before
  public void setUp() {

    customer = ModelFactory.testCustomer();

    SmtpData smtpData = new SmtpData();
    when(receiversManager.get(AlertReceiver.TargetType.Email.name())).thenReturn(emailReceiver);
    when(emailHelper.getDestinations(customer.uuid))
        .thenReturn(Collections.singletonList("to@to.com"));
    when(emailHelper.getSmtpData(customer.uuid)).thenReturn(smtpData);

    alertService = new AlertService();
    alertDefinitionService = new AlertDefinitionService(alertService);
    alertDefinitionGroupService =
        new AlertDefinitionGroupService(alertDefinitionService, configFactory);
    alertRouteService = new AlertRouteService(alertDefinitionGroupService);
    alertManager =
        new AlertManager(
            emailHelper,
            alertService,
            alertDefinitionGroupService,
            alertRouteService,
            receiversManager);
    when(actorSystem.scheduler()).thenReturn(mock(Scheduler.class));
    queryAlerts =
        new QueryAlerts(
            executionContext,
            actorSystem,
            alertService,
            queryHelper,
            alertDefinitionService,
            alertDefinitionGroupService,
            alertManager);

    universe = ModelFactory.createUniverse(customer.getCustomerId());
    when(configFactory.forUniverse(universe)).thenReturn(universeConfig);

    definition = ModelFactory.createAlertDefinition(customer, universe);
  }

  @Test
  public void testQueryAlertsNewAlert() {
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts())
        .thenReturn(ImmutableList.of(createAlertData(raisedTime, false)));

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert = createAlert(raisedTime, false).setUuid(alerts.get(0).getUuid());
    assertThat(alerts, contains(expectedAlert));
  }

  @Test
  public void testQueryAlertsMultipleSeverities() {
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts())
        .thenReturn(
            ImmutableList.of(
                createAlertData(raisedTime, false),
                createAlertData(raisedTime, false, AlertDefinitionGroup.Severity.WARNING)));

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert = createAlert(raisedTime, false).setUuid(alerts.get(0).getUuid());
    assertThat(alerts, contains(expectedAlert));
  }

  @Test
  public void testQueryAlertsNewAlertWithDefaults() {
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(ImmutableList.of(createAlertData(raisedTime, true)));

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    assertThat(alerts, hasSize(1));

    Alert expectedAlert = createAlert(raisedTime, true).setUuid(alerts.get(0).getUuid());
    assertThat(alerts, contains(expectedAlert));
  }

  @Test
  public void testQueryAlertsExistingAlert() {
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts())
        .thenReturn(ImmutableList.of(createAlertData(raisedTime, false)));

    Alert alert = createAlert(raisedTime, true);
    alertService.save(alert);

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert =
        createAlert(raisedTime, false)
            .setUuid(alert.getUuid())
            .setState(Alert.State.ACTIVE)
            .setTargetState(Alert.State.ACTIVE);
    assertThat(alerts, contains(expectedAlert));
  }

  @Test
  public void testQueryAlertsExistingResolvedAlert() {
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts())
        .thenReturn(ImmutableList.of(createAlertData(raisedTime, false)));

    Alert alert = createAlert(raisedTime, true);
    alert.setTargetState(Alert.State.RESOLVED);
    alertService.save(alert);

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    assertThat(alerts, hasSize(2));
  }

  @Test
  public void testQueryAlertsResolveExistingAlert() {
    ZonedDateTime raisedTime = ZonedDateTime.parse("2018-07-04T20:27:12.60602144+02:00");
    when(queryHelper.queryAlerts()).thenReturn(Collections.emptyList());

    Alert alert = createAlert(raisedTime, true);
    alertService.save(alert);

    queryAlerts.scheduleRunner();

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(customer.getUuid())
            .definitionUuid(definition.getUuid())
            .build();
    List<Alert> alerts = alertService.list(alertFilter);

    Alert expectedAlert =
        createAlert(raisedTime, true)
            .setUuid(alert.getUuid())
            .setState(Alert.State.RESOLVED)
            .setTargetState(Alert.State.RESOLVED)
            .setResolvedTime(alerts.get(0).getResolvedTime());
    assertThat(alerts, contains(expectedAlert));
  }

  private Alert createAlert(ZonedDateTime raisedTime, boolean defaults) {
    Alert expectedAlert =
        new Alert()
            .setCreateTime(Date.from(raisedTime.toInstant()))
            .setCustomerUUID(customer.getUuid())
            .setDefinitionUuid(definition.getUuid())
            .setErrCode(KnownAlertCodes.CUSTOMER_ALERT)
            .setGroupUuid(definition.getGroupUUID())
            .setGroupType(AlertDefinitionGroup.TargetType.UNIVERSE)
            .setSeverity(AlertDefinitionGroup.Severity.SEVERE)
            .setMessage("Clock Skew Alert for universe Test is firing")
            .setSendEmail(true)
            .setState(Alert.State.ACTIVE)
            .setTargetState(Alert.State.ACTIVE)
            .setLabel(KnownAlertLabels.CUSTOMER_UUID, customer.getUuid().toString())
            .setLabel(KnownAlertLabels.DEFINITION_UUID, definition.getUuid().toString())
            .setLabel(KnownAlertLabels.GROUP_UUID, definition.getGroupUUID().toString())
            .setLabel(KnownAlertLabels.GROUP_TYPE, AlertDefinitionGroup.TargetType.UNIVERSE.name())
            .setLabel(KnownAlertLabels.DEFINITION_NAME, "Clock Skew Alert")
            .setLabel(KnownAlertLabels.ERROR_CODE, KnownAlertCodes.CUSTOMER_ALERT.name())
            .setLabel(KnownAlertLabels.SEVERITY, AlertDefinitionGroup.Severity.SEVERE.name());
    if (!defaults) {
      expectedAlert.setLabel(KnownAlertLabels.DEFINITION_ACTIVE, "true");
    }
    return expectedAlert;
  }

  private AlertData createAlertData(ZonedDateTime raisedTime, boolean defaults) {
    return createAlertData(raisedTime, defaults, AlertDefinitionGroup.Severity.SEVERE);
  }

  private AlertData createAlertData(
      ZonedDateTime raisedTime, boolean defaults, AlertDefinitionGroup.Severity severity) {
    Map<String, String> labels = new HashMap<>();
    labels.put("customer_uuid", customer.getUuid().toString());
    labels.put("definition_uuid", definition.getUuid().toString());
    labels.put("group_uuid", definition.getGroupUUID().toString());
    labels.put("group_type", "UNIVERSE");
    labels.put("definition_name", "Clock Skew Alert");
    labels.put("severity", severity.name());
    if (!defaults) {
      labels.put("definition_active", "true");
      labels.put("error_code", KnownAlertCodes.CUSTOMER_ALERT.name());
    }
    return AlertData.builder()
        .activeAt(raisedTime.withZoneSameInstant(ZoneId.of("UTC")))
        .annotations(ImmutableMap.of("summary", "Clock Skew Alert for universe Test is firing"))
        .labels(labels)
        .state(AlertState.firing)
        .value(1)
        .build();
  }
}
