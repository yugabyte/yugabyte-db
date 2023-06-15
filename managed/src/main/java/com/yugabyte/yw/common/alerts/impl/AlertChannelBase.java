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

import static com.yugabyte.yw.common.alerts.AlertChannelParams.SYSTEM_VARIABLE_PREFIX;
import static com.yugabyte.yw.common.alerts.AlertTemplateSubstitutor.LABELS_PREFIX;

import com.yugabyte.yw.common.alerts.AlertChannelInterface;
import com.yugabyte.yw.common.alerts.AlertNotificationTemplateSubstitutor;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.common.templates.PlaceholderSubstitutor;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.forms.AlertTemplateSystemVariable;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertTemplateVariable;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.Value;
import org.apache.commons.lang3.StringUtils;

public abstract class AlertChannelBase implements AlertChannelInterface {

  protected final AlertTemplateVariableService alertTemplateVariableService;

  protected AlertChannelBase(AlertTemplateVariableService alertTemplateVariableService) {
    this.alertTemplateVariableService = alertTemplateVariableService;
  }

  public static String getNotificationTitle(Alert alert, Context context, boolean escapeHtml) {
    AlertChannel channel = context.getChannel();
    AlertChannelTemplatesExt templates = context.getTemplates();
    String template = null;
    if (channel.getParams() != null) {
      template = channel.getParams().getTitleTemplate();
    }
    if (StringUtils.isEmpty(template) && templates.getChannelTemplates() != null) {
      template = templates.getChannelTemplates().getTitleTemplate();
    }
    if (StringUtils.isEmpty(template)) {
      template = templates.getDefaultTitleTemplate();
    }
    String notificationTemplate = convertToNotificationTemplate(template);
    return alertSubstitutions(alert, context, notificationTemplate, escapeHtml);
  }

  /**
   * Returns the alert notification text according to templates stored in the alert definition and
   * the alert channel. Also does all the necessary substitutions using labels from the alert.
   *
   * @param alert
   * @param context
   * @return
   */
  public static String getNotificationText(Alert alert, Context context, boolean escapeHtml) {
    AlertChannel channel = context.getChannel();
    AlertChannelTemplatesExt templates = context.getTemplates();
    String template = null;
    if (channel.getParams() != null) {
      template = channel.getParams().getTextTemplate();
    }
    if (StringUtils.isEmpty(template) && templates.getChannelTemplates() != null) {
      template = templates.getChannelTemplates().getTextTemplate();
    }
    if (StringUtils.isEmpty(template)) {
      template = templates.getDefaultTextTemplate();
    }
    String notificationTemplate = convertToNotificationTemplate(template);
    return alertSubstitutions(alert, context, notificationTemplate, escapeHtml);
  }

  private static String convertToNotificationTemplate(String template) {
    Map<String, String> systemVariablesToPlaceholderValue =
        Arrays.stream(AlertTemplateSystemVariable.values())
            .collect(
                Collectors.toMap(
                    AlertTemplateSystemVariable::getName,
                    AlertTemplateSystemVariable::getPlaceholderValue));
    PlaceholderSubstitutor substitutor =
        new PlaceholderSubstitutor(
            key -> {
              String replacement;
              if (key.startsWith(SYSTEM_VARIABLE_PREFIX)) {
                replacement = systemVariablesToPlaceholderValue.get(key);
              } else {
                replacement = LABELS_PREFIX + key;
              }
              return "{{ " + replacement + " }}";
            });
    return substitutor.replace(template);
  }

  private static String alertSubstitutions(
      Alert alert, Context context, String template, boolean escapeHtml) {
    AlertChannel channel = context.getChannel();
    ChannelType channelType;
    if (channel.getParams() != null) {
      channelType = channel.getParams().getChannelType();
    } else {
      // Required for notification preview, as we don't have actual channel.
      channelType = context.getTemplates().getChannelTemplates().getType();
    }
    AlertNotificationTemplateSubstitutor substitutor =
        new AlertNotificationTemplateSubstitutor(
            alert,
            channel,
            context.getLabelDefaultValues(),
            channelType == ChannelType.WebHook,
            escapeHtml);
    return substitutor.replace(template).trim();
  }

  @Value
  public static class Context {
    AlertChannel channel;
    AlertChannelTemplatesExt templates;
    Map<String, String> labelDefaultValues;

    public Context(
        AlertChannel channel,
        AlertChannelTemplatesExt templates,
        List<AlertTemplateVariable> variables) {
      this.channel = channel;
      this.templates = templates;
      this.labelDefaultValues =
          variables.stream()
              .collect(
                  Collectors.toMap(
                      AlertTemplateVariable::getName, AlertTemplateVariable::getDefaultValue));
    }
  }
}
