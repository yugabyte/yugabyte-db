// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyString;
import static org.mockito.Mockito.eq;
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

import com.yugabyte.yw.forms.CustomerRegisterFormData.SmtpData;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;

@RunWith(MockitoJUnitRunner.class)
public class AlertManagerTest extends FakeDBApplication {

  private static final String TEST_STATE = "test state";

  private static final String ALERT_TEST_MESSAGE = "Test message";

  private Customer defaultCustomer;

  @Mock
  private EmailHelper emailHelper;

  @InjectMocks
  private AlertManager am;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void testSendEmail_DoesntFail_UniverseRemoved() throws MessagingException {
    doTestSendEmail(UUID.randomUUID(),
        String.format("Common failure for customer '%s', state: %s\nFailure details:\n\n%s",
            defaultCustomer.name, TEST_STATE, ALERT_TEST_MESSAGE));
  }

  @Test
  public void testSendEmail_UniverseExists() throws MessagingException {
    Universe u = ModelFactory.createUniverse();
    doTestSendEmail(u.universeUUID,
        String.format("Common failure for universe '%s', state: %s\nFailure details:\n\n%s",
            u.name, TEST_STATE, ALERT_TEST_MESSAGE));
  }

  private void doTestSendEmail(UUID universeUUID, String expectedContent)
      throws MessagingException {
    Alert alert = Alert.create(defaultCustomer.uuid, universeUUID, Alert.TargetType.UniverseType,
        "errorCode", "Warning", ALERT_TEST_MESSAGE);
    alert.sendEmail = true;

    SmtpData smtpData = configureSmtp();
    am.sendEmail(alert, TEST_STATE);

    verify(emailHelper, times(1)).sendEmail(eq(defaultCustomer), anyString(), anyString(),
        eq(smtpData),
        eq(Collections.singletonMap("text/plain; charset=\"us-ascii\"", expectedContent)));
  }

  @Test
  public void testResolveAlerts_ExactErrorCode() {
    UUID universeUuid = UUID.randomUUID();
    Alert.create(defaultCustomer.uuid, universeUuid, Alert.TargetType.UniverseType, "errorCode",
        "Warning", ALERT_TEST_MESSAGE);
    Alert.create(defaultCustomer.uuid, universeUuid, Alert.TargetType.UniverseType, "errorCode2",
        "Warning", ALERT_TEST_MESSAGE);

    assertEquals(Alert.State.CREATED, Alert.list(defaultCustomer.uuid, "errorCode").get(0).state);
    am.resolveAlerts(defaultCustomer.uuid, universeUuid, "errorCode");
    assertEquals(Alert.State.RESOLVED, Alert.list(defaultCustomer.uuid, "errorCode").get(0).state);
    // Check that another alert was not updated by the first call.
    assertEquals(Alert.State.CREATED, Alert.list(defaultCustomer.uuid, "errorCode2").get(0).state);

    am.resolveAlerts(defaultCustomer.uuid, universeUuid, "errorCode2");
    assertEquals(Alert.State.RESOLVED, Alert.list(defaultCustomer.uuid, "errorCode2").get(0).state);
  }

  @Test
  public void testResolveAlerts_AllErrorCodes() {
    UUID universeUuid = UUID.randomUUID();
    Alert.create(defaultCustomer.uuid, universeUuid, Alert.TargetType.UniverseType, "errorCode",
        "Warning", ALERT_TEST_MESSAGE);
    Alert.create(defaultCustomer.uuid, universeUuid, Alert.TargetType.UniverseType, "errorCode2",
        "Warning", ALERT_TEST_MESSAGE);

    List<Alert> alerts = Alert.list(defaultCustomer.uuid);
    assertEquals(Alert.State.CREATED, alerts.get(0).state);
    assertEquals(Alert.State.CREATED, alerts.get(1).state);

    am.resolveAlerts(defaultCustomer.uuid, universeUuid, "%");

    alerts = Alert.list(defaultCustomer.uuid);
    assertEquals(Alert.State.RESOLVED, alerts.get(0).state);
    assertEquals(Alert.State.RESOLVED, alerts.get(1).state);
  }

  @Test
  public void testSendEmail_OwnAlertsReseted() {
    SmtpData smtpData = configureSmtp();
    Alert.create(defaultCustomer.uuid, smtpData.configUUID, Alert.TargetType.CustomerConfigType,
        AlertManager.ALERT_MANAGER_ERROR_CODE, "Warning", ALERT_TEST_MESSAGE);

    Alert alert = Alert.create(defaultCustomer.uuid, UUID.randomUUID(),
        Alert.TargetType.UniverseType, "errorCode", "Warning", ALERT_TEST_MESSAGE);
    alert.sendEmail = true;

    List<Alert> alerts = Alert.list(defaultCustomer.uuid, AlertManager.ALERT_MANAGER_ERROR_CODE);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.CREATED, alerts.get(0).state);

    am.sendEmail(alert, TEST_STATE);

    alerts = Alert.list(defaultCustomer.uuid, AlertManager.ALERT_MANAGER_ERROR_CODE);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.RESOLVED, alerts.get(0).state);
  }

  @Test
  public void testSendEmail_OwnAlertGenerated() throws MessagingException {
    SmtpData smtpData = configureSmtp();
    Alert alert = Alert.create(defaultCustomer.uuid, UUID.randomUUID(),
        Alert.TargetType.UniverseType, "errorCode", "Warning", ALERT_TEST_MESSAGE);
    alert.sendEmail = true;

    List<Alert> alerts = Alert.list(defaultCustomer.uuid, AlertManager.ALERT_MANAGER_ERROR_CODE);
    assertEquals(0, alerts.size());

    // EmailHelper.sendEmail should fail.
    doThrow(new MessagingException("test")).when(emailHelper).sendEmail(eq(defaultCustomer),
        anyString(), anyString(), eq(smtpData), any());

    am.sendEmail(alert, TEST_STATE);

    alerts = Alert.list(defaultCustomer.uuid, AlertManager.ALERT_MANAGER_ERROR_CODE);
    assertEquals(1, alerts.size());
    assertEquals(Alert.State.CREATED, alerts.get(0).state);
  }

  private SmtpData configureSmtp() {
    SmtpData smtpData = new SmtpData();
    smtpData.configUUID = UUID.randomUUID();
    when(emailHelper.getDestinations(defaultCustomer.uuid))
        .thenReturn(Collections.singletonList("to@to.com"));
    when(emailHelper.getSmtpData(defaultCustomer.uuid)).thenReturn(smtpData);
    return smtpData;
  }
}
