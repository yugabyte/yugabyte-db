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
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelTemplateService;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
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
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class AlertChannelEmailTest extends FakeDBApplication {

  private static final List<String> DEFAULT_EMAILS = Collections.singletonList("test@test.com");

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Customer defaultCustomer;

  @Mock private EmailHelper emailHelper;

  private AlertChannelEmail are;

  AlertTemplateVariableService alertTemplateVariableService;

  AlertChannelTemplateService alertChannelTemplateService;

  AlertChannelTemplatesExt alertChannelTemplatesExt;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    alertChannelTemplateService = app.injector().instanceOf(AlertChannelTemplateService.class);
    alertChannelTemplatesExt =
        alertChannelTemplateService.getWithDefaults(defaultCustomer.getUuid(), ChannelType.Email);
    alertTemplateVariableService = app.injector().instanceOf(AlertTemplateVariableService.class);

    are = new AlertChannelEmail(emailHelper, alertTemplateVariableService);
  }

  private AlertChannel createChannel(
      boolean defaultRecipients,
      boolean initRecipientsInConfig,
      boolean defaultSmtpSettings,
      boolean initSmtpInConfig) {
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(initRecipientsInConfig ? DEFAULT_EMAILS : null);
    when(emailHelper.getSmtpData(defaultCustomer.getUuid()))
        .thenReturn(initSmtpInConfig ? new SmtpData() : null);

    AlertChannel channel = new AlertChannel();
    AlertChannelEmailParams params = new AlertChannelEmailParams();
    params.setDefaultRecipients(defaultRecipients);
    params.setRecipients(defaultRecipients ? null : DEFAULT_EMAILS);
    params.setDefaultSmtpSettings(defaultSmtpSettings);
    params.setSmtpData(defaultSmtpSettings ? null : new SmtpData());
    channel.setParams(params);
    return channel;
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
      throws MessagingException, PlatformNotificationException {

    AlertChannel channel =
        createChannel(
            defaultRecipients, initRecipientsInConfig, defaultSmtpSettings, initSmtpInConfig);

    Alert alert = ModelFactory.createAlert(defaultCustomer);
    if (exceptionExpected) {
      assertThrows(
          PlatformNotificationException.class,
          () -> {
            are.sendNotification(defaultCustomer, alert, channel, alertChannelTemplatesExt);
          });
    } else {
      are.sendNotification(defaultCustomer, alert, channel, alertChannelTemplatesExt);
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
  public void testSendNotification_ChannelWithoutDefaults_SendFailed() throws MessagingException {
    AlertChannel channel = createChannel(false, false, false, false);
    doThrow(new MessagingException("TestException"))
        .when(emailHelper)
        .sendEmail(any(), any(), any(), any(), any());

    Alert alert = ModelFactory.createAlert(defaultCustomer);
    assertThrows(
        PlatformNotificationException.class,
        () -> {
          are.sendNotification(defaultCustomer, alert, channel, alertChannelTemplatesExt);
        });
  }
}
