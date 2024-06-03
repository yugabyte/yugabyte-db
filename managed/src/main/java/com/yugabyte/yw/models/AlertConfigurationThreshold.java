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

import com.yugabyte.yw.models.common.Condition;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
@ApiModel(
    value = "AlertConfigurationThreshold",
    description =
        "Alert configuration threshold. Conditions can be either greater than a specified value, or"
            + " less than a specified value.")
public class AlertConfigurationThreshold {

  @NotNull
  @ApiModelProperty(
      value = "Threshold condition (greater than, or less than)",
      accessMode = READ_WRITE)
  private Condition condition;

  @NotNull
  @ApiModelProperty(value = "Threshold value", accessMode = READ_WRITE)
  private Double threshold;
}
