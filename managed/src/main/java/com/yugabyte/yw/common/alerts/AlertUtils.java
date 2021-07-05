// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import com.fasterxml.jackson.annotation.JsonTypeName;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.lang.reflect.InvocationTargetException;

public class AlertUtils {
  public static final Logger LOG = LoggerFactory.getLogger(AlertUtils.class);

  @VisibleForTesting
  static final String DEFAULT_ALERT_NOTIFICATION_TITLE = "Yugabyte Platform Alert - <%s>";

  @VisibleForTesting
  static final String DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE =
      "{{ $labels.definition_name }} for {{ $labels.universe_name }} is {{ $labels.alert_state }}.";

  /**
   * Returns the alert notification title according to the template stored in the alert receiver or
   * default one. Also does all the necessary substitutions using labels from the alert.
   *
   * @param alert Alert
   * @param receiver Alert Receiver
   * @return the notification title
   */
  public static String getNotificationTitle(Alert alert, AlertReceiver receiver) {
    String template = receiver.getParams().titleTemplate;
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
   * the alert receiver. Also does all the necessary substitutions using labels from the alert.
   *
   * @param alert
   * @param receiver
   * @return
   */
  public static String getNotificationText(Alert alert, AlertReceiver receiver) {
    String template = receiver.getParams().textTemplate;
    if (StringUtils.isEmpty(template)) {
      if (alert.getDefinitionUUID() == null) {
        return getDefaultNotificationText(alert);
      }
      template = DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE;
    }
    return alertSubstitutions(alert, template);
  }

  @VisibleForTesting
  static String getDefaultNotificationText(Alert alert) {
    String targetType = alert.getLabelValue(KnownAlertLabels.TARGET_TYPE);
    String targetName = alert.getLabelValue(KnownAlertLabels.TARGET_NAME);
    return String.format(
        "Common failure for %s '%s', state: %s\nFailure details:\n\n%s",
        targetType, targetName, alert.getState().getAction(), alert.getMessage());
  }

  public static Class<?> getAlertParamsClass(AlertReceiver.TargetType targetType) {
    switch (targetType) {
      case Email:
        return AlertReceiverEmailParams.class;
      case Slack:
        return AlertReceiverSlackParams.class;
      default:
        return AlertReceiverParams.class;
    }
  }

  /**
   * Creates an instance of a class descendant from AlertReceiverParams. The class is specified by a
   * value of the targetType parameter.
   *
   * @param targetType
   * @return
   */
  public static AlertReceiverParams createParamsInstance(AlertReceiver.TargetType targetType) {
    try {
      return (AlertReceiverParams)
          getAlertParamsClass(targetType).getDeclaredConstructor().newInstance();
    } catch (InstantiationException
        | IllegalAccessException
        | IllegalArgumentException
        | InvocationTargetException
        | NoSuchMethodException
        | SecurityException e) {
      return null;
    }
  }

  public static String getJsonTypeName(AlertReceiverParams params) {
    Class<?> clz = params.getClass();
    JsonTypeName an = clz.getDeclaredAnnotation(JsonTypeName.class);
    return an.value();
  }

  public static void validate(AlertReceiver receiver) throws YWValidateException {
    if (receiver.getParams() == null) {
      throw new YWValidateException("Incorrect parameters in AlertReceiver.");
    }
    receiver.getParams().validate();
  }
}
