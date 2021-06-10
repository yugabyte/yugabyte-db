// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

public class YWValidateException extends Exception {

  private static final long serialVersionUID = 1L;

  public YWValidateException(String message) {
    super(message);
  }
}
