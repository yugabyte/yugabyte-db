// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

public class UniverseInProgressException extends RuntimeException {
  public UniverseInProgressException(String message) {
    super(message);
  }
}
