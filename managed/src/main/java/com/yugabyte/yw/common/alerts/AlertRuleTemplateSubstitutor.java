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

import com.yugabyte.yw.common.templates.PlaceholderSubstitutor;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionLabel;
import com.yugabyte.yw.models.AlertTemplateSettings;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.RequiredArgsConstructor;

public class AlertRuleTemplateSubstitutor extends PlaceholderSubstitutor {
  private static final String DEFINITION_NAME = "definition_name";
  private static final String DEFINITION_EXPR = "definition_expr";
  private static final String DURATION = "duration";
  private static final String LABELS = "labels";
  private static final String SUMMARY_TEMPLATE = "summary_template";
  private static final String LABEL_PREFIX = "          ";

  public AlertRuleTemplateSubstitutor(
      AlertConfiguration configuration,
      AlertDefinition definition,
      AlertConfiguration.Severity severity,
      AlertTemplateSettings templateSettings) {
    super(
        key -> {
          switch (key) {
            case DEFINITION_NAME:
              return configuration.getName();
            case DEFINITION_EXPR:
              return definition.getQueryWithThreshold(configuration.getThresholds().get(severity));
            case DURATION:
              return configuration.getDurationSec() + "s";
            case LABELS:
              return definition
                  .getEffectiveLabels(configuration, templateSettings, severity)
                  .stream()
                  .map(label -> LABEL_PREFIX + label.getName() + ": " + label.getValue())
                  .collect(Collectors.joining("\n"));
            case SUMMARY_TEMPLATE:
              AlertConfigurationLabelProvider labelProvider =
                  new AlertConfigurationLabelProvider(
                      configuration, definition, severity, templateSettings);
              AlertTemplateSubstitutor<AlertConfigurationLabelProvider> substitutor =
                  new AlertTemplateSubstitutor<>(labelProvider);
              return substitutor.replace(configuration.getTemplate().getSummaryTemplate());
            default:
              throw new IllegalArgumentException(
                  "Unexpected placeholder " + key + " in rule template file");
          }
        });
  }

  @RequiredArgsConstructor
  private static class AlertConfigurationLabelProvider implements AlertLabelsProvider {

    private final AlertConfiguration alertConfiguration;
    private final AlertDefinition alertDefinition;
    private final AlertConfiguration.Severity severity;
    private final AlertTemplateSettings alertTemplateSettings;

    @Override
    public String getLabelValue(String name) {
      return alertDefinition
          .getEffectiveLabels(alertConfiguration, alertTemplateSettings, severity)
          .stream()
          .filter(label -> name.equals(label.getName()))
          .map(AlertDefinitionLabel::getValue)
          .findFirst()
          .orElse(null);
    }

    @Override
    public String getAnnotationValue(String name) {
      // Don't have annotations in alert config
      return null;
    }

    @Override
    public UUID getUuid() {
      return alertDefinition.getUuid();
    }
  }
}
