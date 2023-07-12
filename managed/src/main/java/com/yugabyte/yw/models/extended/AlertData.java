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
import com.yugabyte.yw.models.Alert;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel(description = "Alert")
public class AlertData {
  @ApiModelProperty(value = "Alert", accessMode = READ_ONLY)
  @JsonUnwrapped
  private Alert alert;

  @ApiModelProperty(value = "Url to alert expression evaluation", accessMode = READ_ONLY)
  private String alertExpressionUrl;

  @ApiModelProperty(
      value = "Flag, which specifies if metrics link should use browser FQDN",
      accessMode = READ_ONLY)
  private Boolean metricsLinkUseBrowserFqdn;
}
