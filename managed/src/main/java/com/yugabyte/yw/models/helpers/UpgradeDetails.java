// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import com.google.common.collect.ImmutableList;
import java.util.List;

public class UpgradeDetails {

  public enum YsqlMajorVersionUpgradeState {
    IN_PROGRESS,
    UPGRADE_COMPLETE,
    ROLLBACK_IN_PROGRESS,
    ROLLBACK_COMPLETE,
  }

  public static List<YsqlMajorVersionUpgradeState> ALLOWED_UPGRADE_STATE_TO_SET_COMPATIBILITY_FLAG =
      ImmutableList.of(
          YsqlMajorVersionUpgradeState.IN_PROGRESS,
          YsqlMajorVersionUpgradeState.ROLLBACK_IN_PROGRESS);

  public static String getMajorUpgradeCompatibilityFlagValue(YsqlMajorVersionUpgradeState state) {
    return ALLOWED_UPGRADE_STATE_TO_SET_COMPATIBILITY_FLAG.contains(state) ? "11" : "0";
  }
}
