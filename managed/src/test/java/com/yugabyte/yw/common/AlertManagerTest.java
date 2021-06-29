// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.common.alerts.*;
import com.yugabyte.yw.common.alerts.impl.AlertReceiverEmail;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;

import javax.mail.MessagingException;
import java.util.Collections;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class AlertManagerTest extends FakeDBApplication {

  private static final String ALERT_ROUTE_NAME = "Test AlertRoute";

  private Customer defaultCustomer;

  @Mock private AlertReceiverEmail emailReceiver;

  @Mock private AlertReceiverManager receiversManager;

  @Mock private EmailHelper emailHelper;

  @Spy private AlertService alertService;

  @InjectMocks private AlertManager am;

  private AlertDefinition definition;

  private Universe universe;

  private AlertNotificationReport report = new AlertNotificationReport();

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    when(receiversManager.get(AlertReceiver.TargetType.Email.name())).thenReturn(emailReceiver);

    universe = ModelFactory.createUniverse();
    definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);

    // Configuring default SMTP configuration.
    SmtpData smtpData = new SmtpData();
    when(emailHelper.getDestinations(defaultCustomer.uuid))
        .thenReturn(Collections.singletonList("to@to.com"));
    when(emailHelper.getSmtpData(defaultCustomer.uuid)).thenReturn(smtpData);
  }

  @Test
  public void testSendEmail() {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    alert.setDefinitionUUID(definition.getUuid());
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
    Alert amAlert =
        ModelFactory.createAlert(
            defaultCustomer, null, null, KnownAlertCodes.ALERT_MANAGER_FAILURE);
    amAlert.setLabel(
        KnownAlertLabels.TARGET_UUID, AlertManager.DEFAULT_ALERT_RECEIVER_UUID.toString());
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
            .label(
                KnownAlertLabels.TARGET_UUID, AlertManager.DEFAULT_ALERT_RECEIVER_UUID.toString())
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

  // TODO: To update the test after AlertDefinitionGroup is introduced.
  // @Test
  public void testSendNotification_TwoEmailRoutes()
      throws MessagingException, YWNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    AlertReceiver receiver1 = createEmailReceiver();
    AlertRoute.create(defaultCustomer.uuid, ALERT_ROUTE_NAME, Collections.singletonList(receiver1));

    AlertReceiver receiver2 = createEmailReceiver();
    AlertRoute.create(defaultCustomer.uuid, ALERT_ROUTE_NAME, Collections.singletonList(receiver2));

    am.sendNotification(alert, report);
    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    verify(emailReceiver, times(2)).sendNotification(any(), any(), any());
  }

  private AlertReceiver createEmailReceiver() {
    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.continueSend = true;
    params.recipients = Collections.singletonList("test@test.com");
    params.smtpData = EmailFixtures.createSmtpData();

    return AlertReceiver.create(defaultCustomer.uuid, "Test AlertReceiver", params);
  }
}
