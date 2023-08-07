/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.filters;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.models.AlertConfiguration;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class AlertTemplateFilter {
  String name;
  AlertConfiguration.TargetType targetType;

  @JsonIgnore
  public boolean matches(AlertTemplateDescription template) {
    if (name != null && !template.getName().equals(name)) {
      return false;
    }
    if (targetType != null && targetType != template.getTargetType()) {
      return false;
    }
    return true;
  }
}
