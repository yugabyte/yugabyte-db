// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import javax.mail.MessagingException;
import java.util.Collections;

import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class AlertReceiverEmailTest extends FakeDBApplication {

  private Customer defaultCustomer;

  @Mock private EmailHelper emailHelper;

  @InjectMocks private AlertReceiverEmail are;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  private AlertReceiver createFilledReceiver() {
    AlertReceiver receiver = new AlertReceiver();
    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.recipients = Collections.singletonList("test@test.com");
    params.smtpData = new SmtpData();
    receiver.setParams(params);
    return receiver;
  }

  @Test
  public void testSendNotification_HappyPath() throws MessagingException, YWNotificationException {
    AlertReceiver receiver = createFilledReceiver();
    AlertReceiverEmailParams params = ((AlertReceiverEmailParams) receiver.getParams());

    Alert alert = ModelFactory.createAlert(defaultCustomer);
    are.sendNotification(defaultCustomer, alert, receiver);
    verify(emailHelper, times(1))
        .sendEmail(
            eq(defaultCustomer),
            anyString(),
            eq(String.join(", ", params.recipients)),
            eq(params.smtpData),
            any());
  }

  @Test
  public void testSendNotification_SendFailed() throws MessagingException {
    AlertReceiver receiver = createFilledReceiver();
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
