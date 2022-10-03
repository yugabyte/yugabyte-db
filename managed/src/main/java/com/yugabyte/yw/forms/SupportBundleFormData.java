// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.EnumSet;

@ApiModel(description = "Support bundle form metadata")
public class SupportBundleFormData {

  @ApiModelProperty(
      value = "Start date to filter logs from",
      required = true,
      example = "2022-01-25")
  public Date startDate;

  @ApiModelProperty(value = "End date to filter logs till", required = true, example = "2022-01-26")
  public Date endDate;

  @ApiModelProperty(
      value = "List of components to be included in the support bundle",
      required = true)
  public EnumSet<ComponentType> components;
}
