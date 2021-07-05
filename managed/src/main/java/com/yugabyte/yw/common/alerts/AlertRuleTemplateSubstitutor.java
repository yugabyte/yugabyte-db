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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.stream.Collectors;

public class AlertRuleTemplateSubstitutor extends PlaceholderSubstitutor {

  private static final Logger LOG = LoggerFactory.getLogger(AlertRuleTemplateSubstitutor.class);

  private static final String DEFINITION_NAME = "definition_name";
  private static final String DEFINITION_EXPR = "definition_expr";
  private static final String DURATION = "duration";
  private static final String LABELS = "labels";
  private static final String SUMMARY_TEMPLATE = "summary_template";
  private static final String LABEL_PREFIX = "          ";

  public AlertRuleTemplateSubstitutor(AlertDefinition definition) {
    super(
        key -> {
          switch (key) {
            case DEFINITION_NAME:
              return definition.getName();
            case DEFINITION_EXPR:
              return definition.getQueryWithThreshold();
            case DURATION:
              return definition.getQueryDurationSec() + "s";
            case LABELS:
              return definition
                  .getEffectiveLabels()
                  .stream()
                  .map(label -> LABEL_PREFIX + label.getName() + ": " + label.getValue())
                  .collect(Collectors.joining("\n"));
            case SUMMARY_TEMPLATE:
              return definition.getMessageTemplate();
            default:
              throw new IllegalArgumentException(
                  "Unexpected placeholder " + key + " in rule template file");
          }
        });
  }
}
