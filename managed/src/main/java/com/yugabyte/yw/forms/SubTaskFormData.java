// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.helpers.TaskDetails.TaskErrorCode;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.UUID;

@ApiModel(value = "SubTaskFormData", description = "Detailed subtask data")
public class SubTaskFormData {
  @ApiModelProperty(value = "Subtask UUID")
  public UUID subTaskUUID;

  @ApiModelProperty(value = "Subtask creation time")
  public Date creationTime;

  @ApiModelProperty(value = "Subtask type")
  public String subTaskType;

  @ApiModelProperty(value = "Subtask state")
  public String subTaskState;

  @ApiModelProperty(value = "Subtask group type")
  public String subTaskGroupType;

  @ApiModelProperty(value = "Subtask error message")
  public String errorString;

  @ApiModelProperty(value = "WARNING: This is a preview API that could change. Subtask error code")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.0.0")
  public TaskErrorCode errorCode;
}
