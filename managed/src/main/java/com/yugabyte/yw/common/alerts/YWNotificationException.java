// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

public class YWNotificationException extends Exception {

  private static final long serialVersionUID = 1L;

  public YWNotificationException(String message) {
    super(message);
  }

  public YWNotificationException(String message, Exception cause) {
    super(message, cause);
  }
}
