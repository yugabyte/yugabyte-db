/*
 * Copyright 2024 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers.apiModels;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

@ApiModel(description = "Master tablet load balancer status")
public class MasterLBStateResponse {
  @ApiModelProperty(
      required = true,
      value = "YbaApi Internal Whether master tablet load balancer is enabled")
  public Boolean isEnabled;

  @ApiModelProperty(
      required = false,
      value = "YbaApi Internal Whether master tablet load balancer is inactive")
  public Boolean isIdle;

  @ApiModelProperty(
      required = false,
      value =
          "YbaApi Internal Estimate of time for which master tablet load balancer will be active")
  public Long estTimeToBalanceSecs;
}
