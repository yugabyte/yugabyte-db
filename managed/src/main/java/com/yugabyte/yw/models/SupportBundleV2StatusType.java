// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import io.ebean.annotation.DbEnumValue;

public enum SupportBundleV2StatusType {
  Running("Running"),
  Success("Success"),
  Failed("Failed"),
  Aborted("Aborted");

  private final String status;

  SupportBundleV2StatusType(String status) {
    this.status = status;
  }

  @DbEnumValue
  @Override
  public String toString() {
    return this.status;
  }
}
