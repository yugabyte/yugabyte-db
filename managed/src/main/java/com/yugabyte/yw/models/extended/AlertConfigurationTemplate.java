/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.extended;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.AlertConfiguration;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel(description = "Alert configuration template")
public class AlertConfigurationTemplate {
  @ApiModelProperty(value = "Alert configuration with template defaults", accessMode = READ_ONLY)
  @JsonUnwrapped
  private AlertConfiguration defaultConfiguration;

  @ApiModelProperty(value = "Alert threshold minimal value", accessMode = READ_ONLY)
  private double thresholdMinValue;

  @ApiModelProperty(value = "Alert threshold maximal value", accessMode = READ_ONLY)
  private double thresholdMaxValue;

  @ApiModelProperty(value = "Is alert threshold integer or floating point", accessMode = READ_ONLY)
  private boolean thresholdInteger;

  @ApiModelProperty(value = "Is alert threshold read-only or configurable", accessMode = READ_ONLY)
  private boolean thresholdReadOnly;

  @ApiModelProperty(
      value = "Is alert threshold condition read-only or configurable",
      accessMode = READ_ONLY)
  private boolean thresholdConditionReadOnly;

  @ApiModelProperty(value = "Threshold unit name", accessMode = READ_ONLY)
  private String thresholdUnitName;
}
