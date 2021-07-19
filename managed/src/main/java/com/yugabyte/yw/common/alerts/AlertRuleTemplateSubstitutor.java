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
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertDefinitionGroupThreshold;
import com.yugabyte.yw.models.AlertDefinitionLabel;
import java.text.DecimalFormat;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.RequiredArgsConstructor;

public class AlertRuleTemplateSubstitutor extends PlaceholderSubstitutor {

  private static final String QUERY_THRESHOLD_PLACEHOLDER = "{{ query_threshold }}";
  private static final String QUERY_CONDITION_PLACEHOLDER = "{{ query_condition }}";
  private static final DecimalFormat THRESHOLD_FORMAT = new DecimalFormat("0.#");
  private static final String DEFINITION_NAME = "definition_name";
  private static final String DEFINITION_EXPR = "definition_expr";
  private static final String DURATION = "duration";
  private static final String LABELS = "labels";
  private static final String SUMMARY_TEMPLATE = "summary_template";
  private static final String LABEL_PREFIX = "          ";

  public AlertRuleTemplateSubstitutor(
      AlertDefinitionGroup group,
      AlertDefinition definition,
      AlertDefinitionGroup.Severity severity) {
    super(
        key -> {
          switch (key) {
            case DEFINITION_NAME:
              return group.getName();
            case DEFINITION_EXPR:
              return getQueryWithThreshold(
                  definition.getQuery(), group.getThresholds().get(severity));
            case DURATION:
              return group.getDurationSec() + "s";
            case LABELS:
              return definition
                  .getEffectiveLabels(group, severity)
                  .stream()
                  .map(label -> LABEL_PREFIX + label.getName() + ": " + label.getValue())
                  .collect(Collectors.joining("\n"));
            case SUMMARY_TEMPLATE:
              AlertDefinitionGroupLabelProvider labelProvider =
                  new AlertDefinitionGroupLabelProvider(group, definition, severity);
              AlertTemplateSubstitutor<AlertDefinitionGroupLabelProvider> substitutor =
                  new AlertTemplateSubstitutor<>(labelProvider);
              return substitutor.replace(
                  "{{ $labels.definition_name }} Alert for {{ $labels.target_name }} is firing");
            default:
              throw new IllegalArgumentException(
                  "Unexpected placeholder " + key + " in rule template file");
          }
        });
  }

  public static String getQueryWithThreshold(
      String query, AlertDefinitionGroupThreshold threshold) {
    return query
        .replace(QUERY_THRESHOLD_PLACEHOLDER, THRESHOLD_FORMAT.format(threshold.getThreshold()))
        .replace(QUERY_CONDITION_PLACEHOLDER, threshold.getCondition().getValue());
  }

  @RequiredArgsConstructor
  private static class AlertDefinitionGroupLabelProvider implements AlertLabelsProvider {

    private final AlertDefinitionGroup alertDefinitionGroup;
    private final AlertDefinition alertDefinition;
    private final AlertDefinitionGroup.Severity severity;

    @Override
    public String getLabelValue(String name) {
      return alertDefinition
          .getEffectiveLabels(alertDefinitionGroup, severity)
          .stream()
          .filter(label -> name.equals(label.getName()))
          .map(AlertDefinitionLabel::getValue)
          .findFirst()
          .orElse(null);
    }

    @Override
    public UUID getUuid() {
      return alertDefinition.getUuid();
    }
  }
}
