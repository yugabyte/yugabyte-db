package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import play.data.validation.Constraints.MaxLength;
import play.data.validation.Constraints.Pattern;

@ApiModel(description = "xcluster edit form")
public class XClusterConfigEditFormData {

  @MaxLength(256)
  @ApiModelProperty(value = "Name")
  public String name;

  @Pattern("^(Running|Paused)$")
  @ApiModelProperty(value = "Status", allowableValues = "Running, Paused")
  public String status;

  @ApiModelProperty(value = "Source Universe table IDs")
  public Set<String> tables;
}
