// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

/** Per-AZ status for software upgrade progress (canary and standard). */
@ApiModel(description = "Availability zone upgrade status")
public enum AZUpgradeStatus {
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Upgrade for this AZ has not started")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  NOT_STARTED,

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Upgrade for this AZ is in progress"
              + " (persisted before that AZ's upgrade subtasks run; cleared to COMPLETED when they"
              + " finish, or FAILED on task failure / YBA restart)")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  IN_PROGRESS,

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Upgrade for this AZ is completed")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  COMPLETED,

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Upgrade for this AZ failed (subtask"
              + " failure, or was IN_PROGRESS when the upgrade task failed / platform restarted)")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  FAILED
}
