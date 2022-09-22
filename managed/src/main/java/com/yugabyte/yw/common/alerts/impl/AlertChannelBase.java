/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts.impl;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.alerts.AlertChannelInterface;
import com.yugabyte.yw.common.alerts.AlertTemplateSubstitutor;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertChannel;
import org.apache.commons.lang3.StringUtils;

public abstract class AlertChannelBase implements AlertChannelInterface {

  @VisibleForTesting
  static final String DEFAULT_ALERT_NOTIFICATION_TITLE_TEMPLATE =
      "YugabyteDB Anywhere {{ $labels.severity }} alert {{ $labels.definition_name }} "
          + "fired for {{ $labels.source_name }}";

  @VisibleForTesting
  static final String DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE =
      "{{ $labels.definition_name }} alert with severity level '{{ $labels.severity }}' "
          + "for {{ $labels.source_type }} '{{ $labels.source_name }}' "
          + "is {{ $labels.alert_state }}.";

  /**
   * Returns the alert notification title according to the template stored in the alert channel or
   * default one. Also does all the necessary substitutions using labels from the alert.
   *
   * @param alert Alert
   * @param channel Alert Channel
   * @return the notification title
   */
  @VisibleForTesting
  String getNotificationTitle(Alert alert, AlertChannel channel) {
    String template = channel.getParams().getTitleTemplate();
    if (StringUtils.isEmpty(template)) {
      template = DEFAULT_ALERT_NOTIFICATION_TITLE_TEMPLATE;
    }
    return alertSubstitutions(alert, template);
  }

  /**
   * Returns the alert notification text according to templates stored in the alert definition and
   * the alert channel. Also does all the necessary substitutions using labels from the alert.
   *
   * @param alert
   * @param channel
   * @return
   */
  @VisibleForTesting
  String getNotificationText(Alert alert, AlertChannel channel) {
    String template = channel.getParams().getTextTemplate();
    if (StringUtils.isEmpty(template)) {
      template = DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE;
      if (alert.getState() == State.ACTIVE) {
        template = template + "\n\n" + StringUtils.abbreviate(alert.getMessage(), 1000);
      }
    }
    return alertSubstitutions(alert, template);
  }

  private static String alertSubstitutions(Alert alert, String template) {
    AlertTemplateSubstitutor<Alert> substitutor = new AlertTemplateSubstitutor<>(alert);
    return substitutor.replace(template);
  }
}
