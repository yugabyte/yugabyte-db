// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.not;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertNotificationReport;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverManager;
import com.yugabyte.yw.common.alerts.AlertRouteService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.MetricService;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.common.alerts.impl.AlertReceiverEmail;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.Collections;
import java.util.Date;
import javax.mail.MessagingException;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import junitparams.naming.TestCaseName;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class AlertManagerTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static final String DEFAULT_EMAIL = "to@to.com";

  private static final String ALERT_ROUTE_NAME = "Test AlertRoute";

  private Customer defaultCustomer;

  @Mock private AlertReceiverEmail emailReceiver;

  @Mock private AlertReceiverManager receiversManager;

  @Mock private EmailHelper emailHelper;

  private MetricService metricService;

  private AlertService alertService;

  private AlertDefinitionService alertDefinitionService;

  private AlertDefinitionGroupService alertDefinitionGroupService;

  private AlertManager am;

  private AlertDefinitionGroup group;

  private AlertDefinition definition;

  private Universe universe;

  private AlertNotificationReport report = new AlertNotificationReport();

  private AlertRoute defaultRoute;
  private AlertReceiver defaultReceiver;

  private AlertRouteService alertRouteService;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    when(receiversManager.get(AlertReceiver.TargetType.Email.name())).thenReturn(emailReceiver);

    universe = ModelFactory.createUniverse();
    group = ModelFactory.createAlertDefinitionGroup(defaultCustomer, universe);
    definition = ModelFactory.createAlertDefinition(defaultCustomer, universe, group);

    metricService = new MetricService();
    alertService = new AlertService();
    alertDefinitionService = new AlertDefinitionService(alertService);
    alertDefinitionGroupService =
        new AlertDefinitionGroupService(
            alertDefinitionService, new SettableRuntimeConfigFactory(app.config()));
    alertRouteService = new AlertRouteService(alertDefinitionGroupService);
    am =
        new AlertManager(
            emailHelper,
            alertService,
            alertDefinitionGroupService,
            alertRouteService,
            receiversManager,
            metricService);

    defaultRoute = alertRouteService.createDefaultRoute(defaultCustomer.uuid);
    defaultReceiver = defaultRoute.getReceiversList().get(0);
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.singletonList(DEFAULT_EMAIL));
  }

  @Test
  public void testResolveAlerts() {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    assertThat(alert.getState(), is(State.CREATED));
    assertThat(alert.getTargetState(), is(State.ACTIVE));

    am.transitionAlert(alert);

    alert = alertService.get(alert.getUuid());

    assertThat(alert.getState(), is(State.ACTIVE));
    assertThat(alert.getTargetState(), is(State.ACTIVE));

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(defaultCustomer.getUuid())
            .definitionUuid(alert.getDefinitionUuid())
            .build();
    alertService.markResolved(alertFilter);

    alert = alertService.get(alert.getUuid());

    assertThat(alert.getState(), is(State.ACTIVE));
    assertThat(alert.getTargetState(), is(State.RESOLVED));

    am.transitionAlert(alert);

    alert = alertService.get(alert.getUuid());

    assertThat(alert.getState(), is(State.RESOLVED));
    assertThat(alert.getTargetState(), is(State.RESOLVED));
  }

  @Test
  public void testSendNotification_MetricsSetOk() {
    metricService.setStatusMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, defaultCustomer),
        "Some error");
    am.setReceiverStatusMetric(
        PlatformMetrics.ALERT_MANAGER_RECEIVER_STATUS, defaultReceiver, "Some receiver error");

    Alert alert = ModelFactory.createAlert(defaultCustomer);

    am.sendNotificationForState(alert, State.ACTIVE, report);

    Metric amStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
                .targetUuid(defaultCustomer.getUuid())
                .build(),
            1.0);
    assertThat(amStatus.getLabelValue(KnownAlertLabels.ERROR_MESSAGE), nullValue());
    Metric receiverStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_RECEIVER_STATUS.getMetricName())
                .targetUuid(defaultReceiver.getUuid())
                .build(),
            1.0);
    assertThat(receiverStatus.getLabelValue(KnownAlertLabels.ERROR_MESSAGE), nullValue());
  }

  @Test
  public void testSendNotification_FailureMetric() throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer);

    ArgumentCaptor<Alert> captor = ArgumentCaptor.forClass(Alert.class);
    doThrow(new YWNotificationException("test"))
        .when(emailReceiver)
        .sendNotification(eq(defaultCustomer), captor.capture(), any());
    am.sendNotificationForState(alert, State.ACTIVE, report);
    assertThat(captor.getValue().getUuid(), equalTo(alert.getUuid()));

    Metric receiverStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_RECEIVER_STATUS.getMetricName())
                .targetUuid(defaultReceiver.getUuid())
                .build(),
            0.0);
    assertThat(
        receiverStatus.getLabelValue(KnownAlertLabels.ERROR_MESSAGE),
        equalTo("Error sending notification: test"));
  }

  @Test
  public void testSendNotification_AlertWoDefinition_SendEmailOldManner()
      throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    am.sendNotificationForState(alert, State.ACTIVE, report);

    ArgumentCaptor<Alert> captor = ArgumentCaptor.forClass(Alert.class);
    verify(emailReceiver, times(1)).sendNotification(eq(defaultCustomer), captor.capture(), any());
    assertThat(captor.getValue().getUuid(), is(alert.getUuid()));
  }

  @Test
  public void testSendNotification_NoRoutes() throws MessagingException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);
    am.sendNotificationForState(alert, State.ACTIVE, report);
    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
  }

  @Test
  public void testSendNotification_TwoEmailRoutes()
      throws MessagingException, YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    AlertReceiver receiver1 = ModelFactory.createEmailReceiver(defaultCustomer, "AlertReceiver 1");
    AlertReceiver receiver2 = ModelFactory.createEmailReceiver(defaultCustomer, "AlertReceiver 2");
    AlertRoute route =
        ModelFactory.createAlertRoute(
            defaultCustomer.uuid, ALERT_ROUTE_NAME, ImmutableList.of(receiver1, receiver2));
    group.setRouteUUID(route.getUuid());
    group.save();

    am.sendNotificationForState(alert, State.ACTIVE, report);
    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    verify(emailReceiver, times(2)).sendNotification(any(), any(), any());
  }

  @Test
  public void testDefaultRoute_IsUsed() throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);

    am.sendNotificationForState(alert, State.ACTIVE, report);
    ArgumentCaptor<AlertReceiver> receiverCaptor = ArgumentCaptor.forClass(AlertReceiver.class);
    verify(emailReceiver, times(1)).sendNotification(any(), any(), receiverCaptor.capture());

    assertThat(AlertUtils.getJsonTypeName(receiverCaptor.getValue().getParams()), is("Email"));
    AlertReceiverEmailParams params =
        (AlertReceiverEmailParams) receiverCaptor.getValue().getParams();
    assertThat(params.recipients, nullValue());
    assertThat(params.defaultRecipients, is(true));
  }

  @Test
  public void testDefaultRoute_EmptyRecipientsAlertResolved() throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.emptyList());

    am.sendNotificationForState(alert, State.ACTIVE, report);
    verify(emailReceiver, never()).sendNotification(any(), any(), any());

    Metric amStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
                .targetUuid(defaultCustomer.getUuid())
                .build(),
            0.0);
    assertThat(
        amStatus.getLabelValue(KnownAlertLabels.ERROR_MESSAGE),
        equalTo(
            "Unable to notify about alert(s) using default route, "
                + "there are no recipients configured in the customer's profile."));

    // Restoring recipients.
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.singletonList(DEFAULT_EMAIL));

    am.sendNotificationForState(alert, State.ACTIVE, report);
    verify(emailReceiver, times(1)).sendNotification(any(), any(), any());

    amStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
                .targetUuid(defaultCustomer.getUuid())
                .build(),
            1.0);
    assertThat(amStatus.getLabelValue(KnownAlertLabels.ERROR_MESSAGE), nullValue());
  }

  // Aren't checking ACKNOWLEDGED in any state fields as such alert should not be
  // scheduled.
  @Parameters({
    // @formatter:off
    "null, ACTIVE, 1",
    "null, RESOLVED, 2",
    "ACTIVE, RESOLVED, 1",
    // @formatter:on
  })
  @TestCaseName(
      "{method}(Last sent state:{0}, current state:{1}, " + "expected notifications count:{2})")
  @Test
  public void testSendNotifications_CountMatched(
      @Nullable State notifiedState, State currentState, int expectedCount)
      throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    alert
        .setState(currentState)
        .setDefinitionUuid(definition.getUuid())
        .setNotifiedState(notifiedState);
    if (expectedCount > 0) {
      alert.setNextNotificationTime(Date.from(new Date().toInstant().minusSeconds(10)));
    }
    alert.save();

    am.sendNotifications();

    ArgumentCaptor<Alert> captor = ArgumentCaptor.forClass(Alert.class);
    verify(emailReceiver, times(expectedCount))
        .sendNotification(eq(defaultCustomer), captor.capture(), any());

    if (expectedCount > 0) {
      assertThat(captor.getValue().getUuid(), equalTo(alert.getUuid()));

      Alert updatedAlert = alertService.get(alert.getUuid());
      assertThat(updatedAlert.getNextNotificationTime(), nullValue());
      assertThat(updatedAlert.getNotificationAttemptTime(), notNullValue());
      assertThat(updatedAlert.getNotificationsFailed(), is(0));
    }
  }

  @Parameters({
    // @formatter:off
    "false, null, ACTIVE, true",
    "true, null, ACTIVE, false",
    "false, ACKNOWLEDGED, ACTIVE, false",
    "false, null, ACKNOWLEDGED, false",
    // @formatter:on
  })
  @TestCaseName(
      "{method}(Last sent state:{2}, current state:{3}, has nextNotTime:{0}, "
          + "sendNotif:{1}, reschedule expected:{4})")
  @Test
  public void testScheduleAlertNotification(
      boolean hasNextNotificationTime,
      @Nullable State notifiedState,
      State currentState,
      boolean rescheduleExpected) {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    alert
        .setState(currentState)
        .setDefinitionUuid(definition.getUuid())
        .setNotifiedState(notifiedState);
    Date prevDate = null;
    if (hasNextNotificationTime) {
      prevDate = new Date();
      alert.setNextNotificationTime(prevDate);
    }
    alert.save();
    am.scheduleAlertNotification(alert);
    if (rescheduleExpected) {
      assertThat(alert.getNextNotificationTime(), not(prevDate));
    } else {
      assertThat(alert.getNextNotificationTime(), is(prevDate));
    }
  }

  @Test
  public void testSendNotificationForState_WithAnotherState() throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    alert.setState(State.RESOLVED);
    alert.save();

    am.sendNotificationForState(alert, State.ACTIVE, report);

    ArgumentCaptor<Alert> captor = ArgumentCaptor.forClass(Alert.class);
    verify(emailReceiver, times(1)).sendNotification(eq(defaultCustomer), captor.capture(), any());
    assertThat(captor.getValue().getState(), is(State.ACTIVE));
  }
}
