// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

public class UpgradeDetails {

  public enum YsqlMajorVersionUpgradeState {
    IN_PROGRESS,
    PRE_FINALIZE,
    FINALIZE_IN_PROGRESS,
    ROLLBACK_IN_PROGRESS,
    ROLLBACK_COMPLETE,
  }
}
