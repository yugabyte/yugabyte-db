package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;

@ApiModel(description = "xcluster edit form")
public class XClusterConfigEditFormData {

  @ApiModelProperty(value = "Name")
  public String name;

  @ApiModelProperty(value = "Status", allowableValues = "Running, Paused")
  public String status;

  @ApiModelProperty(value = "Source Universe table IDs")
  public Set<String> tables;
}
