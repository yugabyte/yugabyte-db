// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.AlertReceiver.TargetType;

import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import java.util.Collections;

import javax.mail.MessagingException;

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
    receiver.setTargetType(TargetType.Email);
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

    Alert alert = Alert.create(defaultCustomer.uuid, "errCode", "", "");
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

    Alert alert = Alert.create(defaultCustomer.uuid, "errCode", "", "");
    assertThrows(
        YWNotificationException.class,
        () -> {
          are.sendNotification(defaultCustomer, alert, receiver);
        });
  }
}
