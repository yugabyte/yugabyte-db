// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

/**
 * Pause point for canary software upgrade (stored in universe prevYBSoftwareConfig).
 *
 * <p>Transitions (driven by {@code SoftwareUpgradeYB} and {@code CanaryUpgradeConfig}):
 *
 * <pre>
 * null -> NOT_PAUSED                 canary upgrade starts
 * NOT_PAUSED -> PAUSED_AFTER_MASTERS pauseAfterMasters=true, all masters done
 * PAUSED_AFTER_MASTERS -> NOT_PAUSED resume
 * NOT_PAUSED -> PAUSED_AFTER_TSERVERS_AZ  step.pauseAfterTserverUpgrade=true, AZ done
 * PAUSED_AFTER_TSERVERS_AZ -> NOT_PAUSED  resume (repeats per AZ with pause)
 * NOT_PAUSED -> null                 upgrade phase completes; progress fields cleared on
 *                                    finalize (or when upgrade goes to Ready without a separate
 *                                    finalize step)
 * </pre>
 *
 * <p>Non-canary upgrades never set this field. The field is persisted via {@code
 * SaveSoftwareUpgradeProgress} and cleared by {@code createClearSoftwareUpgradeProgressTask}
 * (including at the start of {@code createFinalizeUpgradeTasks} when finalize runs).
 */
@ApiModel(description = "Canary software upgrade pause state")
public enum CanaryPauseState {
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Upgrade is not paused at a canary"
              + " point")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  NOT_PAUSED,

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Paused after all masters were"
              + " upgraded")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  PAUSED_AFTER_MASTERS,

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Paused after tservers in an"
              + " availability zone were upgraded")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  PAUSED_AFTER_TSERVERS_AZ
}
