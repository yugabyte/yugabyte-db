// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyString;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.mockito.Mockito.doThrow;

import java.util.Collections;
import java.util.List;
import java.util.UUID;

import javax.mail.MessagingException;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverManager;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.common.alerts.impl.AlertReceiverEmail;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;

@RunWith(MockitoJUnitRunner.class)
public class AlertManagerTest extends FakeDBApplication {

  private static final String TEST_STATE = "test state";

  private static final String ALERT_TEST_MESSAGE = "Test message";

  private Customer defaultCustomer;

  @Mock private AlertReceiverEmail emailReceiver;

  @Mock private AlertReceiverManager receiversManager;

  @Mock private EmailHelper emailHelper;

  @InjectMocks private AlertManager am;

  private AlertDefinition definition;

  private Universe universe;

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
  public void testSendEmail_DoesntFail_UniverseRemoved() throws MessagingException {
    doTestSendEmail(
        UUID.randomUUID(),
        String.format(
            "Common failure for customer '%s', state: %s\nFailure details:\n\n%s",
            defaultCustomer.name, TEST_STATE, ALERT_TEST_MESSAGE));
  }

  @Test
  public void testSendEmail_UniverseExists() throws MessagingException {
    doTestSendEmail(
        universe.universeUUID,
        String.format(
            "Common failure for universe '%s', state: %s\nFailure details:\n\n%s",
            universe.name, TEST_STATE, ALERT_TEST_MESSAGE));
  }

  private void doTestSendEmail(UUID universeUUID, String expectedContent)
      throws MessagingException {
    Alert alert =
        Alert.create(
            defaultCustomer.uuid,
            universeUUID,
            Alert.TargetType.UniverseType,
            "errorCode",
            "Warning",
            ALERT_TEST_MESSAGE);
    alert.sendEmail = true;
    alert.setDefinitionUUID(definition.getUuid());
    alert.save();

    am.sendNotification(alert);

    try {
      verify(emailReceiver, times(1)).sendNotification(eq(defaultCustomer), eq(alert), any());
    } catch (YWNotificationException e) {
      fail("Unexpected exception caught.");
    }
  }

  @Test
  public void testResolveAlerts_ExactErrorCode() {
    UUID universeUuid = UUID.randomUUID();
    Alert.create(
        defaultCustomer.uuid,
        universeUuid,
        Alert.TargetType.UniverseType,
        "errorCode",
        "Warning",
        ALERT_TEST_MESSAGE);
    Alert.create(
        defaultCustomer.uuid,
        universeUuid,
        Alert.TargetType.UniverseType,
        "errorCode2",
        "Warning",
        ALERT_TEST_MESSAGE);

    assertEquals(
        Alert.State.CREATED, Alert.list(defaultCustomer.uuid, "errorCode").get(0).getState());
    am.resolveAlerts(defaultCustomer.uuid, universeUuid, "errorCode");
    assertEquals(
        Alert.State.RESOLVED, Alert.list(defaultCustomer.uuid, "errorCode").get(0).getState());
    // Check that another alert was not updated by the first call.
    assertEquals(
        Alert.State.CREATED, Alert.list(defaultCustomer.uuid, "errorCode2").get(0).getState());

    am.resolveAlerts(defaultCustomer.uuid, universeUuid, "errorCode2");
    assertEquals(
        Alert.State.RESOLVED, Alert.list(defaultCustomer.uuid, "errorCode2").get(0).getState());
  }

  @Test
  public void testResolveAlerts_AllErrorCodes() {
    UUID universeUuid = UUID.randomUUID();
    Alert.create(
        defaultCustomer.uuid,
        universeUuid,
        Alert.TargetType.UniverseType,
        "errorCode",
        "Warning",
        ALERT_TEST_MESSAGE);
    Alert.create(
        defaultCustomer.uuid,
        universeUuid,
        Alert.TargetType.UniverseType,
        "errorCode2",
        "Warning",
        ALERT_TEST_MESSAGE);

    List<Alert> alerts = Alert.list(defaultCustomer.uuid);
    assertEquals(Alert.State.CREATED, alerts.get(0).getState());
    assertEquals(Alert.State.CREATED, alerts.get(1).getState());

    am.resolveAlerts(defaultCustomer.uuid, universeUuid, "%");

    alerts = Alert.list(defaultCustomer.uuid);
    assertEquals(Alert.State.RESOLVED, alerts.get(0).getState());
    assertEquals(Alert.State.RESOLVED, alerts.get(1).getState());
  }

  @Test
  public void testSendEmail_OwnAlertsReseted() {
    Alert.create(
        defaultCustomer.uuid,
        AlertManager.DEFAULT_ALERT_RECEIVER_UUID,
        Alert.TargetType.AlertReceiverType,
        AlertManager.ALERT_MANAGER_ERROR_CODE,
        "Warning",
        ALERT_TEST_MESSAGE);

    Alert alert =
        Alert.create(
            defaultCustomer.uuid,
            UUID.randomUUID(),
            Alert.TargetType.UniverseType,
            "errorCode",
            "Warning",
            ALERT_TEST_MESSAGE);
    alert.setDefinitionUUID(definition.getUuid());
    alert.sendEmail = true;
    alert.save();

    List<Alert> alerts = Alert.list(defaultCustomer.uuid, AlertManager.ALERT_MANAGER_ERROR_CODE);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.CREATED, alerts.get(0).getState());

    am.sendNotification(alert);

    alerts = Alert.list(defaultCustomer.uuid, AlertManager.ALERT_MANAGER_ERROR_CODE);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.RESOLVED, alerts.get(0).getState());
  }

  @Test
  public void testSendEmail_OwnAlertGenerated() throws MessagingException, YWNotificationException {
    Alert alert =
        Alert.create(
            defaultCustomer.uuid,
            UUID.randomUUID(),
            Alert.TargetType.UniverseType,
            "errorCode",
            "Warning",
            ALERT_TEST_MESSAGE);
    alert.sendEmail = true;
    alert.setDefinitionUUID(definition.getUuid());
    alert.save();

    List<Alert> alerts = Alert.list(defaultCustomer.uuid, AlertManager.ALERT_MANAGER_ERROR_CODE);
    assertEquals(0, alerts.size());

    doThrow(new YWNotificationException("test"))
        .when(emailReceiver)
        .sendNotification(eq(defaultCustomer), eq(alert), any());
    am.sendNotification(alert);

    alerts = Alert.list(defaultCustomer.uuid, AlertManager.ALERT_MANAGER_ERROR_CODE);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.CREATED, alerts.get(0).getState());
  }

  @Test
  public void testSendNotification_AlertWoDefinition_SendEmailOldManner()
      throws MessagingException {
    Alert alert =
        Alert.create(
            defaultCustomer.uuid,
            UUID.randomUUID(),
            Alert.TargetType.UniverseType,
            "errorCode",
            "Warning",
            ALERT_TEST_MESSAGE);
    alert.sendEmail = true;
    alert.setDefinitionUUID(definition.getUuid());
    alert.save();

    am.sendNotification(alert);

    try {
      verify(emailReceiver, times(1)).sendNotification(eq(defaultCustomer), eq(alert), any());
    } catch (YWNotificationException e) {
      fail("Unexpected exception caught.");
    }
  }

  @Test
  public void testSendNotification_NoRoutes() throws MessagingException {
    Alert alert =
        Alert.create(
            defaultCustomer.uuid,
            UUID.randomUUID(),
            Alert.TargetType.UniverseType,
            "errorCode",
            "Warning",
            ALERT_TEST_MESSAGE);
    alert.setDefinitionUUID(definition.getUuid());
    alert.save();

    am.sendNotification(alert);
    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
  }

  @Test
  public void testSendNotification_TwoEmailRoutes()
      throws MessagingException, YWNotificationException {
    Alert alert =
        Alert.create(
            defaultCustomer.uuid,
            UUID.randomUUID(),
            Alert.TargetType.UniverseType,
            "errorCode",
            "Warning",
            ALERT_TEST_MESSAGE);
    alert.setDefinitionUUID(definition.getUuid());
    alert.save();

    AlertReceiver receiver1 = createEmailReceiver();
    AlertRoute.create(defaultCustomer.uuid, definition.getUuid(), receiver1.getUuid());

    AlertReceiver receiver2 = createEmailReceiver();
    AlertRoute.create(defaultCustomer.uuid, definition.getUuid(), receiver2.getUuid());

    am.sendNotification(alert);
    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    verify(emailReceiver, times(2)).sendNotification(any(), any(), any());
  }

  private AlertReceiver createEmailReceiver() {
    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.continueSend = true;
    params.recipients = Collections.singletonList("test@test.com");
    params.smtpData = EmailFixtures.createSmtpData();

    return AlertReceiver.create(defaultCustomer.uuid, params);
  }
}
