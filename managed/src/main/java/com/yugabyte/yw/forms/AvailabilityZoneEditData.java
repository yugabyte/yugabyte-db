package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

@ApiModel(description = "Model used to edit an availability zone")
public class AvailabilityZoneEditData {

  @ApiModelProperty(value = "AZ subnet")
  public String subnet;

  @ApiModelProperty(value = "AZ secondary subnet")
  public String secondarySubnet;
}
