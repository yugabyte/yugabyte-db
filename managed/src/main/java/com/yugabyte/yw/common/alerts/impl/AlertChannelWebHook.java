// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.alerts.AlertChannelWebHookParams;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.Customer;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.apache.http.HttpStatus;
import play.libs.Json;
import play.libs.ws.WSResponse;

@Slf4j
@Singleton
public class AlertChannelWebHook extends AlertChannelWebBase {

  public static final String WEBHOOK_WS_KEY = "yb.alert.webhook.ws";

  @Inject
  public AlertChannelWebHook(
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
    AlertChannelWebHookParams params = (AlertChannelWebHookParams) channel.getParams();

    if (!params.isSendResolved() && alert.getState() == Alert.State.RESOLVED) {
      return;
    }

    List<AlertTemplateVariable> variables = alertTemplateVariableService.list(customer.getUuid());
    Context context = new Context(channel, channelTemplates, variables);
    String text = getNotificationText(alert, context, false);
    JsonNode body = Json.parse(text);

    try {
      WSResponse response =
          sendRequest(WEBHOOK_WS_KEY, params.getWebhookUrl(), body, params.getHttpAuth());

      // To be on the safe side - just accept all 2XX responses as success
      if (response.getStatus() < HttpStatus.SC_OK
          || response.getStatus() >= HttpStatus.SC_MULTIPLE_CHOICES) {
        throw new PlatformNotificationException(
            String.format(
                "Error sending WebHook message for alert %s:"
                    + " error response %s received with body %s",
                alert.getName(), response.getStatus(), response.getBody()));
      }
    } catch (PlatformNotificationException pne) {
      throw pne;
    } catch (Exception e) {
      throw new PlatformNotificationException(
          String.format(
              "Unexpected error sending WebHook event for alert %s: %s",
              alert.getName(), e.getMessage()),
          e);
    }
  }
}
