// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.rbac.enums;

public enum SourceType {
  ENDPOINT("endpoint"),
  REQUEST_BODY("requestBody"),
  DB("db"),
  REQUEST_CONTEXT("requestContext");

  private final String type;

  public String getType() {
    return this.type;
  }

  private SourceType(String type) {
    this.type = type;
  }
}
