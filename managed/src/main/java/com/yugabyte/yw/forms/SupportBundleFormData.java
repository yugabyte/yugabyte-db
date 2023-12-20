// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonFormat;
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
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date startDate;

  @ApiModelProperty(
      value = "End date to filter logs till",
      required = true,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date endDate;

  @ApiModelProperty(
      value = "List of components to be included in the support bundle",
      required = true)
  public EnumSet<ComponentType> components;

  @ApiModelProperty(
      value = "Max number of the most recent cores to collect (if any)",
      required = false)
  public int maxNumRecentCores = 1;

  @ApiModelProperty(
      value = "Max size in bytes of the recent collected cores (if any)",
      required = false)
  public long maxCoreFileSize = 25000000000L;
}
