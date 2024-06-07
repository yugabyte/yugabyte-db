// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator.annotations;

public enum OperatorResourceTypes {
  UNIVERSE("universe"),
  PROVIDER("provider");

  private final String type;

  public String getType() {
    return this.type;
  }

  private OperatorResourceTypes(String type) {
    this.type = type;
  }
}
