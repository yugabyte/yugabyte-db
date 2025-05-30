// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static com.yugabyte.yw.common.Util.getYbaVersion;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;

public class AbstractTaskParams implements ITaskParams {

  @JsonProperty private String platformVersion = getYbaVersion();

  @ApiModelProperty(value = "Previous task UUID of a retry")
  private UUID previousTaskUUID;

  @ApiModelProperty(value = "Error message")
  private String errorString = null;

  @Override
  public void setErrorString(String errorString) {
    this.errorString = errorString;
  }

  @Override
  public String getErrorString() {
    return this.errorString;
  }

  @Override
  public void setPreviousTaskUUID(UUID previousTaskUUID) {
    this.previousTaskUUID = previousTaskUUID;
  }

  @Override
  public UUID getPreviousTaskUUID() {
    return previousTaskUUID;
  }

  @JsonIgnore
  @Override
  public UUID getTargetUuid(TaskType taskType) {
    return null;
  }

  public String getPlatformVersion() {
    return this.platformVersion;
  }
}
