// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.UUID;

@ApiModel(value = "Customer Task Data", description = "Customer Task data")
public class CustomerTaskFormData {

  @ApiModelProperty(value = "Customer Task UUID")
  public UUID id;

  @ApiModelProperty(value = "Customer Task title", example = "Deleted Universe : test-universe")
  public String title;

  @ApiModelProperty(value = "Customer Task percentage completed", example = "100")
  public int percentComplete;

  @ApiModelProperty(value = "Customer Task creation time", example = "1624295417405")
  public Date createTime;

  @ApiModelProperty(value = "Customer Task completion time", example = "1624295417405")
  public Date completionTime;

  @ApiModelProperty(value = "Customer Task target", example = "Universe")
  public String target;

  @ApiModelProperty(value = "Customer Task target UUID")
  public UUID targetUUID;

  @ApiModelProperty(value = "Customer Task type", example = "Delete")
  public String type;

  @ApiModelProperty(value = "Customer Task status", example = "Complete")
  public String status;
}
