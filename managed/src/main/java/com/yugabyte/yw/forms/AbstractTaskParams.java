// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;

public class AbstractTaskParams implements ITaskParams {

  @ApiModelProperty(value = "Previous task UUID of a retry")
  private UUID previousTaskUUID;

  @ApiModelProperty(
      value =
          "UUID of the first task in the retry/rollback chain (clean universe state). Carried"
              + " forward on retries and rollbacks. Distinct from previousTaskUUID, which is the"
              + " immediate predecessor used for runtimeInfo inherit.")
  private UUID originalTaskUUID;

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

  @Override
  public void setOriginalTaskUUID(UUID originalTaskUUID) {
    this.originalTaskUUID = originalTaskUUID;
  }

  @Override
  public UUID getOriginalTaskUUID() {
    return originalTaskUUID;
  }

  @JsonIgnore
  @Override
  public UUID getTargetUuid(TaskType taskType) {
    return null;
  }
}
