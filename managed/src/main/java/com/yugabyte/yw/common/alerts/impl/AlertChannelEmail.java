// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelInterface;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.Customer;
import java.util.Collections;
import java.util.List;
import javax.mail.MessagingException;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;

@Slf4j
@Singleton
public class AlertChannelEmail implements AlertChannelInterface {

  @Inject private EmailHelper emailHelper;

  @Override
  public void sendNotification(Customer customer, Alert alert, AlertChannel channel)
      throws PlatformNotificationException {
    log.debug("sendNotification {}", alert);
    AlertChannelEmailParams params = (AlertChannelEmailParams) channel.getParams();
    String title = AlertUtils.getNotificationTitle(alert, channel);
    String text = AlertUtils.getNotificationText(alert, channel);

    SmtpData smtpData =
        params.defaultSmtpSettings ? emailHelper.getSmtpData(customer.uuid) : params.smtpData;
    List<String> recipients =
        params.defaultRecipients ? emailHelper.getDestinations(customer.uuid) : params.recipients;

    if (CollectionUtils.isEmpty(recipients)) {
      throw new PlatformNotificationException(
          String.format(
              "Error sending email for alert %s: No recipients found for channel %s",
              alert.getName(), channel.getName()));
    }

    if (smtpData == null) {
      throw new PlatformNotificationException(
          String.format(
              "Error sending email for alert %s: SMTP settings not found for channel %s.",
              alert.getName(), channel.getName()));
    }

    try {
      emailHelper.sendEmail(
          customer,
          title,
          String.join(", ", recipients),
          smtpData,
          Collections.singletonMap("text/plain; charset=\"us-ascii\"", text));
    } catch (MessagingException e) {
      throw new PlatformNotificationException(
          String.format("Error sending email for alert %s: %s", alert.getName(), e.getMessage()),
          e);
    }
  }
}
