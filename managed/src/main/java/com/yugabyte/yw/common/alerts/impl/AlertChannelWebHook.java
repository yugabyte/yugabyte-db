// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.WSClientRefresher;
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
import org.apache.http.HttpStatus;
import play.libs.ws.WSResponse;

@Slf4j
@Singleton
public class AlertChannelWebHook extends AlertChannelWebBase {

  public static final String WEBHOOK_WS_KEY = "yb.alert.webhook.ws";

  @Inject
  public AlertChannelWebHook(WSClientRefresher wsClientRefresher) {
    super(wsClientRefresher);
  }

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

      WSResponse response = sendRequest(WEBHOOK_WS_KEY, params.getWebhookUrl(), message);

      if (response.getStatus() != HttpStatus.SC_OK) {
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
