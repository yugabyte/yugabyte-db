// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonFormat;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import com.fasterxml.jackson.databind.JsonNode;
import java.util.Date;
import java.util.UUID;

@ApiModel(value = "CustomerTaskData", description = "Customer task data")
public class CustomerTaskFormData {

  @ApiModelProperty(value = "Customer task UUID")
  public UUID id;

  @ApiModelProperty(value = "Customer task title", example = "Deleted Universe : test-universe")
  public String title;

  @ApiModelProperty(value = "Customer task percentage completed", example = "100")
  public int percentComplete;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssZ")
  @ApiModelProperty(value = "Customer task creation time", example = "2021-06-17T15:00:05-0400")
  public Date createTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssZ")
  @ApiModelProperty(value = "Customer task completion time", example = "2021-06-17T15:00:05-0400")
  public Date completionTime;

  @ApiModelProperty(value = "Customer task target", example = "Universe")
  public String target;

  @ApiModelProperty(value = "Customer task target UUID")
  public UUID targetUUID;

  @ApiModelProperty(value = "Customer task type", example = "Delete")
  public String type;

  @ApiModelProperty(value = "Customer task type name", example = "Software Upgrade")
  public String typeName;

  @ApiModelProperty(value = "Customer task status", example = "Complete")
  public String status;

  @ApiModelProperty(value = "Customer task details", example = "2.4.3.0 => 2.7.1.1")
  public JsonNode details;

  @ApiModelProperty(value = "Customer task abortable")
  public boolean abortable;

  @ApiModelProperty(value = "Customer task retryable")
  public boolean retryable;
}
