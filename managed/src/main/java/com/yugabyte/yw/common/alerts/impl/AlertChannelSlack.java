// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.alerts.AlertChannelSlackParams;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.Customer;
import java.util.List;
import javax.inject.Inject;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.http.HttpStatus;
import play.libs.ws.WSResponse;

@Slf4j
@Singleton
public class AlertChannelSlack extends AlertChannelWebBase {

  public static final String SLACK_WS_KEY = "yb.alert.slack.ws";

  @Inject
  public AlertChannelSlack(
      WSClientRefresher wsClientRefresher,
      AlertTemplateVariableService alertTemplateVariableService) {
    super(wsClientRefresher, alertTemplateVariableService);
  }

  @Override
  public void sendNotification(
      Customer customer,
      Alert alert,
      AlertChannel channel,
      AlertChannelTemplatesExt channelTemplates)
      throws PlatformNotificationException {
    log.trace("sendNotification {}", alert);
    AlertChannelSlackParams params = (AlertChannelSlackParams) channel.getParams();
    List<AlertTemplateVariable> variables = alertTemplateVariableService.list(customer.getUuid());
    Context context = new Context(channel, channelTemplates, variables);
    String text = getNotificationText(alert, context, false);

    SlackMessage message = new SlackMessage();
    message.username = params.getUsername();
    message.icon_url = params.getIconUrl();
    message.text = text;

    try {
      WSResponse response = sendRequest(SLACK_WS_KEY, params.getWebhookUrl(), message);

      if (response.getStatus() != HttpStatus.SC_OK) {
        throw new PlatformNotificationException(
            String.format(
                "Error sending Slack message for alert %s: error response %s received with body %s",
                alert.getName(), response.getStatus(), response.getBody()));
      }
    } catch (PlatformNotificationException pne) {
      throw pne;
    } catch (Exception e) {
      throw new PlatformNotificationException(
          String.format(
              "Error sending Slack message for alert %s: %s", alert.getName(), e.getMessage()),
          e);
    }
  }

  @Data
  private static class SlackMessage {
    private String username;
    private String text;
    private String icon_url;
  }
}
