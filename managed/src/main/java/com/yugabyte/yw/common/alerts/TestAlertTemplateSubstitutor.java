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

import static com.yugabyte.yw.common.Util.doubleToString;

import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.TestAlertSettings;
import com.yugabyte.yw.common.templates.PlaceholderSubstitutor;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;

public class TestAlertTemplateSubstitutor extends PlaceholderSubstitutor {

  private static final String VALUE = "$value";

  public TestAlertTemplateSubstitutor(
      Alert alert, AlertTemplateDescription template, AlertConfiguration configuration) {
    super(
        key -> {
          if (key.contains(VALUE)) {
            TestAlertSettings settings = template.getTestAlertSettings();
            double value;
            if (settings.isGenerateValueFromThreshold()) {
              double threshold =
                  Double.parseDouble(alert.getLabelValue(KnownAlertLabels.THRESHOLD));
              Severity severity = Severity.valueOf(alert.getLabelValue(KnownAlertLabels.SEVERITY));
              Condition condition = configuration.getThresholds().get(severity).getCondition();
              if (condition == Condition.GREATER_THAN) {
                value = threshold + 1;
              } else {
                value = threshold - 1;
              }
            } else {
              value = settings.getCustomValue();
            }

            return doubleToString(value);
          }
          return "{{ " + key + " }}";
        });
  }
}
