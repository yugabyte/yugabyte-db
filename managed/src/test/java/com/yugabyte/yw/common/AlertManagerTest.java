// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyString;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.eq;
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
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.common.alerts.impl.AlertReceiverEmail;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import javax.mail.MessagingException;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertManagerTest extends FakeDBApplication {

  private static final String DEFAULT_EMAIL = "to@to.com";

  private static final String ALERT_ROUTE_NAME = "Test AlertRoute";

  private Customer defaultCustomer;

  @Mock private AlertReceiverEmail emailReceiver;

  @Mock private AlertReceiverManager receiversManager;

  @Mock private EmailHelper emailHelper;

  private AlertService alertService;

  private AlertDefinitionService alertDefinitionService;

  private AlertDefinitionGroupService alertDefinitionGroupService;

  private AlertManager am;

  private AlertDefinitionGroup group;

  private AlertDefinition definition;

  private Universe universe;

  private AlertNotificationReport report = new AlertNotificationReport();

  private AlertRoute defaultRoute;

  private AlertRouteService alertRouteService;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    when(receiversManager.get(AlertReceiver.TargetType.Email.name())).thenReturn(emailReceiver);

    universe = ModelFactory.createUniverse();
    group = ModelFactory.createAlertDefinitionGroup(defaultCustomer, universe);
    definition = ModelFactory.createAlertDefinition(defaultCustomer, universe, group);

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
            receiversManager);

    defaultRoute = alertRouteService.createDefaultRoute(defaultCustomer.uuid);
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.singletonList(DEFAULT_EMAIL));
  }

  @Test
  public void testSendEmail() {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    alert.setDefinitionUuid(definition.getUuid());
    alert.save();

    am.sendNotification(alert, report);

    try {
      verify(emailReceiver, times(1)).sendNotification(eq(defaultCustomer), eq(alert), any());
    } catch (YWNotificationException e) {
      fail("Unexpected exception caught.");
    }
  }

  @Test
  public void testResolveAlerts() {
    Alert alert = ModelFactory.createAlert(defaultCustomer);

    assertEquals(Alert.State.CREATED, alert.getState());
    assertEquals(Alert.State.ACTIVE, alert.getTargetState());

    am.transitionAlert(alert, report);

    alert = alertService.get(alert.getUuid());

    assertEquals(Alert.State.ACTIVE, alert.getState());
    assertEquals(Alert.State.ACTIVE, alert.getTargetState());

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(defaultCustomer.getUuid())
            .errorCode(KnownAlertCodes.CUSTOMER_ALERT)
            .build();
    alertService.markResolved(alertFilter);

    alert = alertService.get(alert.getUuid());

    assertEquals(Alert.State.ACTIVE, alert.getState());
    assertEquals(Alert.State.RESOLVED, alert.getTargetState());

    am.transitionAlert(alert, report);

    alert = alertService.get(alert.getUuid());

    assertEquals(Alert.State.RESOLVED, alert.getState());
    assertEquals(Alert.State.RESOLVED, alert.getTargetState());
  }

  @Test
  public void testSendEmail_OwnAlertsReseted() {
    UUID defaultReceiverUUID = defaultRoute.getReceivers().get(0);
    Alert amAlert =
        ModelFactory.createAlert(
            defaultCustomer, null, null, KnownAlertCodes.ALERT_MANAGER_FAILURE);
    amAlert.setLabel(KnownAlertLabels.TARGET_UUID, defaultReceiverUUID.toString());
    amAlert.setSendEmail(false);
    alertService.save(amAlert);

    Alert alert = ModelFactory.createAlert(defaultCustomer);

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(defaultCustomer.getUuid())
            .errorCode(KnownAlertCodes.ALERT_MANAGER_FAILURE)
            .build();
    List<Alert> alerts = alertService.list(alertFilter);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.ACTIVE, alerts.get(0).getTargetState());

    am.sendNotification(alert, report);

    alerts = alertService.list(alertFilter);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.RESOLVED, alerts.get(0).getTargetState());
  }

  @Test
  public void testSendEmail_OwnAlertGenerated() throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer);

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(defaultCustomer.getUuid())
            .errorCode(KnownAlertCodes.ALERT_MANAGER_FAILURE)
            .build();
    List<Alert> alerts = alertService.list(alertFilter);
    assertEquals(0, alerts.size());

    doThrow(new YWNotificationException("test"))
        .when(emailReceiver)
        .sendNotification(eq(defaultCustomer), eq(alert), any());
    am.sendNotification(alert, report);

    alerts = alertService.list(alertFilter);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.ACTIVE, alerts.get(0).getTargetState());
  }

  @Test
  public void testSendNotification_AlertWoDefinition_SendEmailOldManner()
      throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    am.sendNotification(alert, report);
    verify(emailReceiver, times(1)).sendNotification(eq(defaultCustomer), eq(alert), any());
  }

  @Test
  public void testSendNotification_NoRoutes() throws MessagingException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);
    am.sendNotification(alert, report);
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

    am.sendNotification(alert, report);
    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    verify(emailReceiver, times(2)).sendNotification(any(), any(), any());
  }

  @Test
  public void testDefaultRoute_IsUsed() throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);

    am.sendNotification(alert, report);
    ArgumentCaptor<AlertReceiver> receiverCaptor = ArgumentCaptor.forClass(AlertReceiver.class);
    verify(emailReceiver, times(1)).sendNotification(any(), any(), receiverCaptor.capture());

    assertEquals("Email", AlertUtils.getJsonTypeName(receiverCaptor.getValue().getParams()));
    AlertReceiverEmailParams params =
        (AlertReceiverEmailParams) receiverCaptor.getValue().getParams();
    assertNull(params.recipients);
    assertTrue(params.defaultRecipients);
  }

  @Test
  public void testDefaultRoute_EmptyRecipientsAlertResolved() throws YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.emptyList());

    AlertFilter alertFilter =
        AlertFilter.builder()
            .customerUuid(defaultCustomer.getUuid())
            .errorCode(KnownAlertCodes.ALERT_MANAGER_FAILURE)
            .targetState(Alert.State.CREATED, Alert.State.ACTIVE)
            .build();
    List<Alert> alerts = alertService.list(alertFilter);
    assertEquals(0, alerts.size());

    am.sendNotification(alert, report);
    verify(emailReceiver, never()).sendNotification(any(), any(), any());

    alerts = alertService.list(alertFilter);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.ACTIVE, alerts.get(0).getTargetState());
    assertEquals(KnownAlertCodes.ALERT_MANAGER_FAILURE.name(), alerts.get(0).getErrCode());

    // Restoring recipients.
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.singletonList(DEFAULT_EMAIL));

    am.sendNotification(alert, report);
    verify(emailReceiver, times(1)).sendNotification(any(), any(), any());
    alerts = alertService.list(alertFilter);
    assertEquals(0, alerts.size());
  }
}
