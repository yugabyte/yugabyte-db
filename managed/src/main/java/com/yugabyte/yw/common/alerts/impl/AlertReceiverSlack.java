// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.alerts.AlertReceiverInterface;
import com.yugabyte.yw.common.alerts.AlertReceiverSlackParams;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;

import java.io.IOException;

import lombok.Data;
import lombok.extern.slf4j.Slf4j;

import org.apache.http.client.methods.HttpPost;
import org.apache.http.entity.StringEntity;
import org.apache.http.impl.client.CloseableHttpClient;
import org.apache.http.impl.client.HttpClients;

@Slf4j
@Singleton
public class AlertReceiverSlack implements AlertReceiverInterface {

  @Override
  public void sendNotification(Customer customer, Alert alert, AlertReceiver receiver)
      throws YWNotificationException {
    log.trace("sendNotification {}", alert);
    AlertReceiverSlackParams params = (AlertReceiverSlackParams) receiver.getParams();
    String title = AlertUtils.getNotificationTitle(alert, receiver);
    String text = AlertUtils.getNotificationText(alert, receiver);

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

      client.execute(httpPost);
    } catch (IOException e) {
      throw new YWNotificationException(
          String.format(
              "Error sending Slack message for alert %s: %s", alert.getUuid(), e.getMessage()),
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
