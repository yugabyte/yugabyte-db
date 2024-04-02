// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonValue;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.alerts.AlertChannelPagerDutyParams;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.Customer;
import java.time.ZoneId;
import java.util.Comparator;
import java.util.List;
import javax.inject.Inject;
import lombok.Builder;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.http.HttpStatus;
import play.libs.Json;
import play.libs.ws.WSResponse;

@Slf4j
@Singleton
public class AlertChannelPagerDuty extends AlertChannelWebBase {
  public static final String PAGERDUTY_WS_KEY = "yb.alert.pagerduty.ws";
  private static final String PAGER_DUTY_EVENT_API = "https://events.pagerduty.com/v2/enqueue";

  private final String eventApiUrl;

  @Inject
  public AlertChannelPagerDuty(
      WSClientRefresher wsClientRefresher,
      AlertTemplateVariableService alertTemplateVariableService) {
    this(PAGER_DUTY_EVENT_API, wsClientRefresher, alertTemplateVariableService);
  }

  @VisibleForTesting
  AlertChannelPagerDuty(
      String eventApiUrl,
      WSClientRefresher wsClientRefresher,
      AlertTemplateVariableService alertTemplateVariableService) {
    super(wsClientRefresher, alertTemplateVariableService);
    this.eventApiUrl = eventApiUrl;
  }

  @Override
  public void sendNotification(
      Customer customer,
      Alert alert,
      AlertChannel channel,
      AlertChannelTemplatesExt channelTemplates)
      throws PlatformNotificationException {
    log.trace("sendNotification {}", alert);
    AlertChannelPagerDutyParams params = (AlertChannelPagerDutyParams) channel.getParams();
    List<AlertTemplateVariable> variables = alertTemplateVariableService.list(customer.getUuid());
    Context context = new Context(channel, channelTemplates, variables);
    String title = getNotificationTitle(alert, context, false);
    String text = getNotificationText(alert, context, false);

    try {
      EventResult eventResult;
      Incident request;
      if (alert.getState() == State.ACTIVE) {
        Severity severity = Severity.ERROR;
        if (alert.getSeverity() == AlertConfiguration.Severity.WARNING) {
          severity = Severity.WARNING;
        }
        ObjectNode customDetails = Json.newObject();
        alert.getEffectiveLabels().stream()
            .sorted(Comparator.comparing(AlertLabel::getName))
            .forEach(label -> customDetails.put(label.getName(), label.getValue()));
        Payload payload =
            Payload.builder()
                .group(alert.getConfigurationType().name())
                .eventClass(title)
                .summary(text)
                .source("YB Platform " + customer.getName())
                .severity(severity)
                .timestamp(
                    alert
                        .getCreateTime()
                        .toInstant()
                        .atZone(ZoneId.systemDefault())
                        .toOffsetDateTime()
                        .toString())
                .customDetails(customDetails)
                .build();

        request =
            Incident.builder()
                .eventAction(EventAction.TRIGGER)
                .routingKey(params.getRoutingKey())
                .payload(payload)
                .dedupKey(alert.getUuid().toString())
                .build();
      } else {
        request =
            Incident.builder()
                .eventAction(EventAction.RESOLVE)
                .routingKey(params.getRoutingKey())
                .dedupKey(alert.getUuid().toString())
                .build();
      }
      eventResult = sendRequest(request);
      if (!StringUtils.isEmpty(eventResult.getErrors())) {
        throw new PlatformNotificationException(
            String.format(
                "Error sending PagerDuty event for alert %s: %s",
                alert.getName(), eventResult.getErrors()));
      }
    } catch (PlatformNotificationException pne) {
      throw pne;
    } catch (Exception e) {
      throw new PlatformNotificationException(
          String.format(
              "Unexpected error sending PagerDuty event for alert %s: %s",
              alert.getName(), e.getMessage()),
          e);
    }
  }

  private EventResult sendRequest(Incident incident) throws Exception {
    WSResponse response = sendRequest(PAGERDUTY_WS_KEY, eventApiUrl, incident);

    int responseStatus = response.getStatus();
    JsonNode jsonResponse;
    switch (responseStatus) {
      case HttpStatus.SC_OK:
      case HttpStatus.SC_CREATED:
      case HttpStatus.SC_ACCEPTED:
        jsonResponse = Json.parse(response.getBody());
        return EventResult.builder()
            .status(getTextOrNull(jsonResponse, "status"))
            .message(getTextOrNull(jsonResponse, "message"))
            .dedupKey(getTextOrNull(jsonResponse, "dedup_key"))
            .build();
      case HttpStatus.SC_BAD_REQUEST:
        jsonResponse = Json.parse(response.getBody());
        return EventResult.builder()
            .status(getTextOrNull(jsonResponse, "status"))
            .message(getTextOrNull(jsonResponse, "message"))
            .errors(getStringOrNull(jsonResponse, "errors"))
            .build();
      default:
        return EventResult.builder()
            .status(String.valueOf(responseStatus))
            .errors(response.getBody())
            .build();
    }
  }

  private String getTextOrNull(JsonNode json, String field) {
    return json.has(field) ? json.get(field).asText() : null;
  }

  private String getStringOrNull(JsonNode json, String field) {
    return json.has(field) ? json.get(field).toString() : null;
  }

  enum Severity {
    CRITICAL("critical"),
    ERROR("error"),
    WARNING("warning"),
    INFO("info");

    private final String severity;

    Severity(String severity) {
      this.severity = severity;
    }

    @JsonValue
    public String getSeverity() {
      return severity;
    }

    @Override
    public String toString() {
      return getSeverity();
    }
  }

  enum EventAction {
    TRIGGER("trigger"),
    ACKNOWLEDGE("acknowledge"),
    RESOLVE("resolve");

    private final String eventAction;

    EventAction(String eventAction) {
      this.eventAction = eventAction;
    }

    @JsonValue
    public String getEventType() {
      return eventAction;
    }

    @Override
    public String toString() {
      return getEventType();
    }
  }

  @Value
  @Builder
  @JsonInclude(JsonInclude.Include.NON_NULL)
  static class Payload {
    String summary;
    String source;
    Severity severity;
    String timestamp;
    String component;
    String group;

    @JsonProperty("class")
    String eventClass;

    @JsonProperty("custom_details")
    ObjectNode customDetails;
  }

  @Value
  @Builder
  @JsonInclude(JsonInclude.Include.NON_NULL)
  static class Incident {
    @JsonProperty("routing_key")
    String routingKey;

    @JsonProperty("event_action")
    EventAction eventAction;

    @JsonProperty("dedup_key")
    String dedupKey;

    Payload payload;
  }

  @Value
  @Builder
  static class EventResult {
    String status;
    String message;
    String dedupKey;
    String errors;
  }
}
