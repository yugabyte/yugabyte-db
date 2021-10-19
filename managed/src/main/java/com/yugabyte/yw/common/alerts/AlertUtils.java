// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.Customer;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class AlertUtils {
  public static final Logger LOG = LoggerFactory.getLogger(AlertUtils.class);

  @VisibleForTesting
  static final String DEFAULT_ALERT_NOTIFICATION_TITLE = "Yugabyte Platform Alert - <%s>";

  @VisibleForTesting
  static final String DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE =
      "{{ $labels.definition_name }} Alert for {{ $labels.source_name }} "
          + "is {{ $labels.alert_state }}.";

  /**
   * Returns the alert notification title according to the template stored in the alert channel or
   * default one. Also does all the necessary substitutions using labels from the alert.
   *
   * @param alert Alert
   * @param channel Alert Channel
   * @return the notification title
   */
  public static String getNotificationTitle(Alert alert, AlertChannel channel) {
    String template = channel.getParams().titleTemplate;
    if (StringUtils.isEmpty(template)) {
      Customer customer = Customer.getOrBadRequest(alert.getCustomerUUID());
      return String.format(DEFAULT_ALERT_NOTIFICATION_TITLE, customer.getTag());
    }
    return alertSubstitutions(alert, template);
  }

  private static String alertSubstitutions(Alert alert, String template) {
    AlertTemplateSubstitutor<Alert> substitutor = new AlertTemplateSubstitutor<>(alert);
    return substitutor.replace(template);
  }

  /**
   * Returns the alert notification text according to templates stored in the alert definition and
   * the alert channel. Also does all the necessary substitutions using labels from the alert.
   *
   * @param alert
   * @param channel
   * @return
   */
  public static String getNotificationText(Alert alert, AlertChannel channel) {
    String template = channel.getParams().textTemplate;
    if (StringUtils.isEmpty(template)) {
      template = DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE;
      if (alert.getState() == State.ACTIVE) {
        template = template + "\n\n" + StringUtils.abbreviate(alert.getMessage(), 1000);
      }
    }
    return alertSubstitutions(alert, template);
  }

  public static Class<?> getAlertParamsClass(ChannelType channelType) {
    switch (channelType) {
      case Email:
        return AlertChannelEmailParams.class;
      case Slack:
        return AlertChannelSlackParams.class;
      default:
        return AlertChannelParams.class;
    }
  }

  public static String getJsonTypeName(AlertChannelParams params) {
    Class<?> clz = params.getClass();
    JsonTypeName an = clz.getDeclaredAnnotation(JsonTypeName.class);
    return an.value();
  }
}
