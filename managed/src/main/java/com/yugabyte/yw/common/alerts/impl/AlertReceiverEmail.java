// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import java.util.Collections;
import javax.mail.MessagingException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverInterface;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;

@Singleton
public class AlertReceiverEmail implements AlertReceiverInterface {

  public static final Logger LOG = LoggerFactory.getLogger(AlertReceiverEmail.class);

  @Inject private EmailHelper emailHelper;

  @Override
  public void sendNotification(Customer customer, Alert alert, AlertReceiver receiver)
      throws YWNotificationException {
    LOG.debug("sendNotification {}", alert);
    AlertReceiverEmailParams params = (AlertReceiverEmailParams) receiver.getParams();
    String title = AlertUtils.getNotificationTitle(alert, receiver);
    String text = AlertUtils.getNotificationText(alert, receiver);

    try {
      emailHelper.sendEmail(
          customer,
          title,
          String.join(",", params.recipients),
          params.smtpData,
          Collections.singletonMap("text/plain; charset=\"us-ascii\"", text));
    } catch (MessagingException e) {
      throw new YWNotificationException(
          String.format("Error sending email for alert %s: %s", alert.getUuid(), e.getMessage()),
          e);
    }
  }
}
