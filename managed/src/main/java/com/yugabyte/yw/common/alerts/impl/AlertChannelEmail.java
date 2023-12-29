// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.Customer;
import jakarta.mail.MessagingException;
import java.util.Collections;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
@Singleton
public class AlertChannelEmail extends AlertChannelBase {

  private final EmailHelper emailHelper;

  @Inject
  public AlertChannelEmail(
      EmailHelper emailHelper, AlertTemplateVariableService alertTemplateVariableService) {
    super(alertTemplateVariableService);
    this.emailHelper = emailHelper;
  }

  @Override
  public void sendNotification(
      Customer customer,
      Alert alert,
      AlertChannel channel,
      AlertChannelTemplatesExt channelTemplates)
      throws PlatformNotificationException {
    log.debug("sendNotification {}", alert);
    AlertChannelEmailParams params = (AlertChannelEmailParams) channel.getParams();
    List<AlertTemplateVariable> variables = alertTemplateVariableService.list(customer.getUuid());
    Context context = new Context(channel, channelTemplates, variables);
    String title = getNotificationTitle(alert, context, false);
    String text = getNotificationText(alert, context, true);

    SmtpData smtpData =
        params.isDefaultSmtpSettings()
            ? emailHelper.getSmtpData(customer.getUuid())
            : params.getSmtpData();
    List<String> recipients =
        params.isDefaultRecipients()
            ? emailHelper.getDestinations(customer.getUuid())
            : params.getRecipients();

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
          Collections.singletonMap("text/html; charset=\"us-ascii\"", text));
    } catch (MessagingException e) {
      throw new PlatformNotificationException(
          String.format("Error sending email for alert %s: %s", alert.getName(), e.getMessage()),
          e);
    }
  }
}
