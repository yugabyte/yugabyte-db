// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverInterface;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;
import java.util.Collections;
import java.util.List;
import javax.mail.MessagingException;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;

@Slf4j
@Singleton
public class AlertReceiverEmail implements AlertReceiverInterface {

  @Inject private EmailHelper emailHelper;

  @Override
  public void sendNotification(Customer customer, Alert alert, AlertReceiver receiver)
      throws YWNotificationException {
    log.debug("sendNotification {}", alert);
    AlertReceiverEmailParams params = (AlertReceiverEmailParams) receiver.getParams();
    String title = AlertUtils.getNotificationTitle(alert, receiver);
    String text = AlertUtils.getNotificationText(alert, receiver);

    SmtpData smtpData =
        params.defaultSmtpSettings ? emailHelper.getSmtpData(customer.uuid) : params.smtpData;
    List<String> recipients =
        params.defaultRecipients ? emailHelper.getDestinations(customer.uuid) : params.recipients;

    if (CollectionUtils.isEmpty(recipients)) {
      throw new YWNotificationException(
          String.format("Error sending email for alert %s: No recipients found.", alert.getUuid()));
    }

    if (smtpData == null) {
      throw new YWNotificationException(
          String.format(
              "Error sending email for alert %s: Invalid SMTP settings found.", alert.getUuid()));
    }

    try {
      emailHelper.sendEmail(
          customer,
          title,
          String.join(", ", recipients),
          smtpData,
          Collections.singletonMap("text/plain; charset=\"us-ascii\"", text));
    } catch (MessagingException e) {
      throw new YWNotificationException(
          String.format("Error sending email for alert %s: %s", alert.getUuid(), e.getMessage()),
          e);
    }
  }
}
