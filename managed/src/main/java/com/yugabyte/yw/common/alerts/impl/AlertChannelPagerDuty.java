// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.github.dikhan.pagerduty.client.events.PagerDutyEventsClient;
import com.github.dikhan.pagerduty.client.events.domain.EventResult;
import com.github.dikhan.pagerduty.client.events.domain.Payload;
import com.github.dikhan.pagerduty.client.events.domain.ResolveIncident;
import com.github.dikhan.pagerduty.client.events.domain.Severity;
import com.github.dikhan.pagerduty.client.events.domain.TriggerIncident;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.alerts.AlertChannelPagerDutyParams;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.Customer;
import java.time.ZoneId;
import java.util.Comparator;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.json.JSONObject;

@Slf4j
@Singleton
public class AlertChannelPagerDuty extends AlertChannelBase {

  private volatile PagerDutyEventsClient pagerDutyEventsClient;

  public AlertChannelPagerDuty() {}

  @VisibleForTesting
  AlertChannelPagerDuty(String apiUrl) {
    this.pagerDutyEventsClient = PagerDutyEventsClient.create(apiUrl);
  }

  @Override
  public void sendNotification(Customer customer, Alert alert, AlertChannel channel)
      throws PlatformNotificationException {
    log.trace("sendNotification {}", alert);
    AlertChannelPagerDutyParams params = (AlertChannelPagerDutyParams) channel.getParams();
    String title = getNotificationTitle(alert, channel);
    String text = getNotificationText(alert, channel);

    try {
      PagerDutyEventsClient client = getPagerDutyEventsClient();
      EventResult eventResult;
      if (alert.getState() == State.ACTIVE) {
        Severity severity = Severity.ERROR;
        if (alert.getSeverity() == AlertConfiguration.Severity.WARNING) {
          severity = Severity.WARNING;
        }
        JSONObject customDetails = new JSONObject();
        alert
            .getLabels()
            .stream()
            .sorted(Comparator.comparing(AlertLabel::getName))
            .forEach(label -> customDetails.put(label.getName(), label.getValue()));
        Payload payload =
            Payload.Builder.newBuilder()
                .setGroup(alert.getConfigurationType().name())
                .setEventClass(title)
                .setSummary(text)
                .setSource("YB Platform " + customer.name)
                .setSeverity(severity)
                .setTimestamp(
                    alert
                        .getCreateTime()
                        .toInstant()
                        .atZone(ZoneId.systemDefault())
                        .toOffsetDateTime())
                .setCustomDetails(customDetails)
                .build();

        TriggerIncident incident =
            TriggerIncident.TriggerIncidentBuilder.newBuilder(params.getRoutingKey(), payload)
                .setDedupKey(alert.getUuid().toString())
                .build();
        eventResult = client.trigger(incident);
      } else {
        ResolveIncident resolveIncident =
            ResolveIncident.ResolveIncidentBuilder.newBuilder(
                    params.getRoutingKey(), alert.getUuid().toString())
                .build();
        eventResult = client.resolve(resolveIncident);
      }
      if (!StringUtils.isEmpty(eventResult.getErrors())) {
        throw new PlatformNotificationException(
            String.format(
                "Error sending PagerDuty event for alert %s: %s",
                alert.getName(), eventResult.getErrors()));
      }
    } catch (Exception e) {
      throw new PlatformNotificationException(
          String.format(
              "Unexpected error sending PagerDuty event for alert %s: %s",
              alert.getName(), e.getMessage()),
          e);
    }
  }

  private PagerDutyEventsClient getPagerDutyEventsClient() {
    if (pagerDutyEventsClient == null) {
      synchronized (this) {
        if (pagerDutyEventsClient == null) {
          pagerDutyEventsClient = PagerDutyEventsClient.create();
        }
      }
    }
    return pagerDutyEventsClient;
  }
}
