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
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class AlertDefinitionTemplateFilter {
  String name;
  AlertDefinitionGroup.TargetType targetType;

  @JsonIgnore
  public boolean matches(AlertDefinitionTemplate template) {
    if (name != null && !template.getName().equals(name)) {
      return false;
    }
    if (targetType != null && targetType != template.getTargetType()) {
      return false;
    }
    return true;
  }
}
