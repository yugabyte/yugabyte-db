// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

/** Marker interface. All task params implement this interface. */
public interface ITaskParams {
  void setErrorString(String errorString);

  String getErrorString();
}
