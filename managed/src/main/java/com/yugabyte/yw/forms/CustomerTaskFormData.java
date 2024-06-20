// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.TaskInfo;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.UUID;

@ApiModel(value = "CustomerTaskData", description = "Customer task data")
public class CustomerTaskFormData {

  @ApiModelProperty(value = "Customer task UUID")
  public UUID id;

  @ApiModelProperty(value = "Customer task title", example = "Deleted Universe : test-universe")
  public String title;

  @ApiModelProperty(value = "Customer task percentage completed", example = "100")
  public int percentComplete;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Customer task creation time", example = "2022-12-12T13:07:18Z")
  public Date createTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Customer task completion time", example = "2022-12-12T13:07:18Z")
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

  @ApiModelProperty(value = "Correlation id")
  public String correlationId;

  @ApiModelProperty(value = "Customer Email", accessMode = READ_ONLY)
  public String userEmail;

  @ApiModelProperty(value = "Task info", hidden = true)
  public TaskInfo taskInfo;

  @ApiModelProperty(value = "Subtask infos", hidden = true)
  public List<TaskInfo> subtaskInfos;
}
