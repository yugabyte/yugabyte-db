// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static com.yugabyte.yw.models.YugawareProperty.get;

import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;

public class AbstractTaskParams implements ITaskParams {

  public String platformVersion = platformVersionFromDB();

  @ApiModelProperty(value = "Previous task UUID of a retry")
  private UUID previousTaskUUID;

  @ApiModelProperty(value = "Error message")
  public String errorString = null;

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

  public String getPlatformVersion() {
    return platformVersion;
  }

  private String platformVersionFromDB() {
    try {
      return get("SoftwareVersion").getValue().get("version").asText();
    } catch (NullPointerException e) {
      return "";
    }
  }
}
