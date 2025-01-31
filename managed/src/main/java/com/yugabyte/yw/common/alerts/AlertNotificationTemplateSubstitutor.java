/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import static com.yugabyte.yw.common.alerts.AlertTemplateSubstitutor.ANNOTATIONS_PREFIX;
import static com.yugabyte.yw.common.alerts.AlertTemplateSubstitutor.LABELS_PREFIX;

import com.fasterxml.jackson.core.io.JsonStringEncoder;
import com.yugabyte.yw.common.templates.PlaceholderSubstitutor;
import com.yugabyte.yw.forms.AlertTemplateSystemVariable;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertLabel;
import java.text.SimpleDateFormat;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.text.StringEscapeUtils;
import play.libs.Json;

@Slf4j
public class AlertNotificationTemplateSubstitutor {

  private final SimpleDateFormat DATE_FORMAT = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ssXXX");

  private final Alert alert;
  private final AlertChannel alertChannel;

  private final Map<String, String> placeholderDefaultValues;

  private final boolean json;

  private final PlaceholderSubstitutor placeholderSubstitutor;

  private final JsonStringEncoder jsonStringEncoder;

  private final boolean escapeHtml;

  public AlertNotificationTemplateSubstitutor(
      Alert alert,
      AlertChannel alertChannel,
      Map<String, String> labelDefaultValues,
      boolean json,
      boolean escapeHtml) {
    this.alert = alert;
    this.alertChannel = alertChannel;
    this.placeholderDefaultValues = labelDefaultValues;
    this.json = json;
    this.escapeHtml = escapeHtml;
    jsonStringEncoder = JsonStringEncoder.getInstance();
    placeholderSubstitutor =
        new PlaceholderSubstitutor("{{", "}}", key -> getPlaceholderValue(key));
  }

  private String getPlaceholderValue(String key) {
    if (key.equals(AlertTemplateSystemVariable.YUGABYTE_ALERT_UUID.getPlaceholderValue())) {
      return processValue(alert.getUuid().toString(), json);
    }
    if (key.equals(AlertTemplateSystemVariable.YUGABYTE_ALERT_CHANNEL_NAME.getPlaceholderValue())) {
      return processValue(alertChannel.getName(), json);
    }
    if (key.equals(AlertTemplateSystemVariable.YUGABYTE_ALERT_LABELS_JSON.getPlaceholderValue())) {
      Map<String, String> labels =
          alert.getEffectiveLabels().stream()
              .collect(Collectors.toMap(AlertLabel::getName, AlertLabel::getValue));
      return Json.stringify(Json.toJson(labels));
    }
    if (key.equals(AlertTemplateSystemVariable.YUGABYTE_ALERT_START_TIME.getPlaceholderValue())) {
      return processValue(DATE_FORMAT.format(alert.getCreateTime()), json);
    }
    if (key.equals(AlertTemplateSystemVariable.YUGABYTE_ALERT_END_TIME.getPlaceholderValue())) {
      String endTime =
          alert.getResolvedTime() != null ? DATE_FORMAT.format(alert.getResolvedTime()) : null;
      return processValue(endTime, json);
    }
    if (key.equals(AlertTemplateSystemVariable.YUGABYTE_ALERT_STATUS.getPlaceholderValue())) {
      return processValue(alert.getState().getAction(), json);
    }
    if (key.startsWith(LABELS_PREFIX)) {
      String labelName = key.replace(LABELS_PREFIX, "");
      String labelValue = alert.getLabelValue(labelName);
      if (labelValue == null) {
        labelValue = placeholderDefaultValues.get(labelName);
      }
      return processValue(labelValue, json);
    }
    if (key.startsWith(ANNOTATIONS_PREFIX)) {
      String annotationName = key.replace(ANNOTATIONS_PREFIX, "");
      return processValue(alert.getAnnotationValue(annotationName), json);
    }
    // Possibly some prometheus expression, which can also contain {{ something }} placeholders
    return "{{ " + key + " }}";
  }

  public String replace(String templateStr) {
    String result = templateStr;
    result = placeholderSubstitutor.replace(result);
    if (json) {
      // As quotes are part of template, and we can't use placeholder substitutor to handle that -
      // we have to handle special case for null values here.
      result = result.replaceAll("\"null\"", "null");
    }
    return result;
  }

  public String processValue(String value, boolean json) {
    if (value != null) {
      if (escapeHtml) {
        value = StringEscapeUtils.escapeHtml4(value);
      }
      if (json) {
        value = new String(jsonStringEncoder.quoteAsString(value));
      }
      return value;
    }
    if (json) {
      return "null";
    } else {
      return StringUtils.EMPTY;
    }
  }
}
