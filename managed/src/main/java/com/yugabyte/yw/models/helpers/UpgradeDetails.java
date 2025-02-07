// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.google.common.collect.ImmutableList;
import java.util.List;

public class UpgradeDetails {

  public enum YsqlMajorVersionUpgradeState {
    IN_PROGRESS,
    PRE_FINALIZE,
    FINALIZE_IN_PROGRESS,
    ROLLBACK_IN_PROGRESS,
    ROLLBACK_COMPLETE,
  }

  public static List<YsqlMajorVersionUpgradeState> ALLOWED_UPGRADE_STATE_TO_SET_COMPATIBILITY_FLAG =
      ImmutableList.of(
          YsqlMajorVersionUpgradeState.IN_PROGRESS,
          YsqlMajorVersionUpgradeState.PRE_FINALIZE,
          YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS);

  public static String getMajorUpgradeCompatibilityFlagValue(YsqlMajorVersionUpgradeState state) {
    return ALLOWED_UPGRADE_STATE_TO_SET_COMPATIBILITY_FLAG.contains(state) ? "11" : "0";
  }
}
