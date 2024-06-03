// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

public class PlatformNotificationException extends RuntimeException {

  private static final long serialVersionUID = 1L;

  public PlatformNotificationException(String message) {
    super(message);
  }

  public PlatformNotificationException(String message, Exception cause) {
    super(message, cause);
  }
}
