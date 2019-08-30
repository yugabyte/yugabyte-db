// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

public class AbstractTaskParams implements ITaskParams {

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
