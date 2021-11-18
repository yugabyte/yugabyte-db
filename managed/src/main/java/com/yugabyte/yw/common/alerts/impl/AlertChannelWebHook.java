// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.alerts.AlertChannelInterface;
import com.yugabyte.yw.common.alerts.AlertChannelWebHookParams;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.common.alerts.impl.AlertManagerWebHookV4.Status;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.util.Collections;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.http.HttpResponse;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.entity.StringEntity;
import org.apache.http.impl.client.CloseableHttpClient;
import org.apache.http.impl.client.HttpClients;

@Slf4j
@Singleton
public class AlertChannelWebHook implements AlertChannelInterface {

  @Inject
  public AlertChannelWebHook() {}

  @Override
  public void sendNotification(Customer customer, Alert alert, AlertChannel channel)
      throws PlatformNotificationException {
    log.trace("sendNotification {}", alert);
    AlertChannelWebHookParams params = (AlertChannelWebHookParams) channel.getParams();

    try {
      Status status = alert.getState() == State.ACTIVE ? Status.firing : Status.resolved;
      ZonedDateTime startAt = alert.getCreateTime().toInstant().atZone(ZoneId.systemDefault());
      ZonedDateTime endAt =
          alert.getResolvedTime() != null
              ? alert.getResolvedTime().toInstant().atZone(ZoneId.systemDefault())
              : null;
      AlertManagerWebHookV4 message =
          AlertManagerWebHookV4.builder()
              .status(status)
              .receiver(channel.getName())
              .groupLabels(
                  ImmutableMap.of(
                      KnownAlertLabels.CONFIGURATION_UUID.labelName(),
                      alert.getLabelValue(KnownAlertLabels.CONFIGURATION_UUID),
                      KnownAlertLabels.DEFINITION_NAME.labelName(),
                      alert.getName()))
              .alerts(
                  Collections.singletonList(
                      AlertManagerWebHookV4.Alert.builder()
                          .status(status)
                          .labels(
                              alert
                                  .getLabels()
                                  .stream()
                                  .collect(
                                      Collectors.toMap(AlertLabel::getName, AlertLabel::getValue)))
                          .annotations(
                              ImmutableMap.of(
                                  KnownAlertLabels.MESSAGE.labelName(), alert.getMessage()))
                          .startsAt(startAt)
                          .endsAt(endAt)
                          .build()))
              .build();
      HttpPost httpPost = new HttpPost(params.getWebhookUrl());
      try (CloseableHttpClient client = HttpClients.createDefault()) {
        ObjectMapper objectMapper = new ObjectMapper();
        objectMapper.registerModule(new JavaTimeModule());
        String json = objectMapper.writeValueAsString(message);

        httpPost.setEntity(new StringEntity(json));
        httpPost.setHeader("Accept", "application/json");
        httpPost.setHeader("Content-type", "application/json");

        HttpResponse response = client.execute(httpPost);

        if (response.getStatusLine().getStatusCode() != 200) {
          throw new PlatformNotificationException(
              String.format(
                  "Error sending WebHook message for alert %s: error response %s received",
                  alert.getName(), response.getStatusLine().getStatusCode()));
        }
      }
    } catch (Exception e) {
      throw new PlatformNotificationException(
          String.format(
              "Unexpected error sending WebHook event for alert %s: %s",
              alert.getName(), e.getMessage()),
          e);
    }
  }
}
