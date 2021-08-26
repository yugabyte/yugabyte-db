// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

public class PlatformValidationException extends Exception {

  private static final long serialVersionUID = 1L;

  public PlatformValidationException(String message) {
    super(message);
  }
}
