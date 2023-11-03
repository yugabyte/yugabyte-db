// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.forms.SubTaskFormData;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.UUID;

@ApiModel(value = "FailedSubtasks", description = "Failed Subtasks")
public class FailedSubtasks {

  // list of all failed subtasks.
  @ApiModelProperty(value = "List of failed subtasks")
  public List<SubtaskData> failedSubTasks;

  public static class SubtaskData {
    @ApiModelProperty(value = "Failed SubTask UUID")
    public UUID subTaskUUID;

    @ApiModelProperty(
        value = "Creation time (unix timestamp) of the task",
        example = "2022-12-12T13:07:18Z")
    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
    public Date creationTime;

    @ApiModelProperty(value = "Failed SubTask Type")
    public String subTaskType;

    @ApiModelProperty(value = "Failed SubTask State")
    public String subTaskState;

    @ApiModelProperty(value = "Failed SubTask Group Type")
    public String subTaskGroupType;

    @ApiModelProperty(value = "Failed SubTask Error message")
    public String errorString;
  }

  public static SubtaskData toSubtaskData(SubTaskFormData subTaskFormData) {
    SubtaskData subtaskData = new SubtaskData();
    subtaskData.subTaskUUID = subTaskFormData.subTaskUUID;
    subtaskData.creationTime = subTaskFormData.creationTime;
    subtaskData.subTaskType = subTaskFormData.subTaskType;
    subtaskData.subTaskState = subTaskFormData.subTaskState;
    subtaskData.subTaskGroupType = subTaskFormData.subTaskGroupType;
    subtaskData.errorString = subTaskFormData.errorString;
    return subtaskData;
  }
}
