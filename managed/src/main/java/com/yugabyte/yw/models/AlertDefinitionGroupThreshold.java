/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
@ApiModel(value = "Alert configuration threshold.")
public class AlertDefinitionGroupThreshold {

  public enum Condition {
    GREATER_THAN(">"),
    LESS_THAN("<");

    private final String value;

    Condition(String value) {
      this.value = value;
    }

    public String getValue() {
      return value;
    }
  }

  @ApiModelProperty(value = "Threshold condition", accessMode = READ_WRITE)
  private Condition condition;

  @ApiModelProperty(value = "Threshold value", accessMode = READ_WRITE)
  private Double threshold;
}
