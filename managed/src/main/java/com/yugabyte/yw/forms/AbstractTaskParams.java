// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModelProperty;

public class AbstractTaskParams implements ITaskParams {

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
}
