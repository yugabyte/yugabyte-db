// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;
import java.util.Collections;
import java.util.List;
import javax.mail.MessagingException;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.naming.TestCaseName;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class AlertReceiverEmailTest extends FakeDBApplication {

  private static final List<String> DEFAULT_EMAILS = Collections.singletonList("test@test.com");

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Customer defaultCustomer;

  @Mock private EmailHelper emailHelper;

  @InjectMocks private AlertReceiverEmail are;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  private AlertReceiver createReceiver(
      boolean defaultRecipients,
      boolean initRecipientsInConfig,
      boolean defaultSmtpSettings,
      boolean initSmtpInConfig) {
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(initRecipientsInConfig ? DEFAULT_EMAILS : null);
    when(emailHelper.getSmtpData(defaultCustomer.getUuid()))
        .thenReturn(initSmtpInConfig ? new SmtpData() : null);

    AlertReceiver receiver = new AlertReceiver();
    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.defaultRecipients = defaultRecipients;
    params.recipients = defaultRecipients ? null : DEFAULT_EMAILS;
    params.defaultSmtpSettings = defaultSmtpSettings;
    params.smtpData = defaultSmtpSettings ? null : new SmtpData();
    receiver.setParams(params);
    return receiver;
  }

  @Test
  @Parameters({
    // @formatter:off
    "false, false, false, false, false",
    "true , true, false, false, false",
    "true, false, false, false, true",
    "false, false, true, true, false",
    "false, false, true, false, true",
    "true, true, true, true, false"
    // @formatter:on
  })
  @TestCaseName(
      "{method}(Default recipients:{0}, Default Smtp:{1}, "
          + "Recipients in config:{2}, Smtp in config:{3})")
  public void testSendNotification(
      boolean defaultRecipients,
      boolean initRecipientsInConfig,
      boolean defaultSmtpSettings,
      boolean initSmtpInConfig,
      boolean exceptionExpected)
      throws MessagingException, YWNotificationException {

    AlertReceiver receiver =
        createReceiver(
            defaultRecipients, initRecipientsInConfig, defaultSmtpSettings, initSmtpInConfig);

    Alert alert = ModelFactory.createAlert(defaultCustomer);
    if (exceptionExpected) {
      assertThrows(
          YWNotificationException.class,
          () -> {
            are.sendNotification(defaultCustomer, alert, receiver);
          });
    } else {
      are.sendNotification(defaultCustomer, alert, receiver);
    }
    verify(emailHelper, exceptionExpected ? never() : times(1))
        .sendEmail(
            eq(defaultCustomer),
            anyString(),
            eq(String.join(", ", DEFAULT_EMAILS)),
            any(SmtpData.class),
            any());
  }

  @Test
  public void testSendNotification_ReceiverWithoutDefaults_SendFailed() throws MessagingException {
    AlertReceiver receiver = createReceiver(false, false, false, false);
    doThrow(new MessagingException("TestException"))
        .when(emailHelper)
        .sendEmail(any(), any(), any(), any(), any());

    Alert alert = ModelFactory.createAlert(defaultCustomer);
    assertThrows(
        YWNotificationException.class,
        () -> {
          are.sendNotification(defaultCustomer, alert, receiver);
        });
  }
}
