// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.alerts.AlertChannelInterface;
import com.yugabyte.yw.common.alerts.AlertChannelSlackParams;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.Customer;
import java.io.IOException;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.http.HttpResponse;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.entity.StringEntity;
import org.apache.http.impl.client.CloseableHttpClient;
import org.apache.http.impl.client.HttpClients;

@Slf4j
@Singleton
public class AlertChannelSlack implements AlertChannelInterface {

  @Override
  public void sendNotification(Customer customer, Alert alert, AlertChannel channel)
      throws PlatformNotificationException {
    log.trace("sendNotification {}", alert);
    AlertChannelSlackParams params = (AlertChannelSlackParams) channel.getParams();
    String title = AlertUtils.getNotificationTitle(alert, channel);
    String text = AlertUtils.getNotificationText(alert, channel);

    SlackMessage message = new SlackMessage();
    message.username = params.username;
    message.icon_url = params.iconUrl;
    message.text = String.format("*%s*\n%s", title, text);

    HttpPost httpPost = new HttpPost(params.webhookUrl);
    try (CloseableHttpClient client = HttpClients.createDefault()) {
      ObjectMapper objectMapper = new ObjectMapper();
      String json = objectMapper.writeValueAsString(message);

      httpPost.setEntity(new StringEntity(json));
      httpPost.setHeader("Accept", "application/json");
      httpPost.setHeader("Content-type", "application/json");

      HttpResponse response = client.execute(httpPost);

      if (response.getStatusLine().getStatusCode() != 200) {
        throw new PlatformNotificationException(
            String.format(
                "Error sending Slack message for alert %s: error response %s received",
                alert.getName(), response.getStatusLine().getStatusCode()));
      }
    } catch (IOException e) {
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
