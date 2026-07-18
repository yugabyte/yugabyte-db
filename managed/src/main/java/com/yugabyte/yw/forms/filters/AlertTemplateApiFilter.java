/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms.filters;

import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.filters.AlertTemplateFilter;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@NoArgsConstructor
public class AlertTemplateApiFilter {
  @ApiModelProperty(value = "The name of the alert template.")
  private String name;

  @ApiModelProperty(value = "The target type of the alert template.")
  private AlertConfiguration.TargetType targetType;

  public AlertTemplateFilter toFilter() {
    AlertTemplateFilter.AlertTemplateFilterBuilder builder = AlertTemplateFilter.builder();
    if (name != null) {
      builder.name(name);
    }
    if (targetType != null) {
      builder.targetType(targetType);
    }
    return builder.build();
  }
}
