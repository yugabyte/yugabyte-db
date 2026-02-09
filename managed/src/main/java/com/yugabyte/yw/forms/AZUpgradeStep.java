// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import play.data.validation.Constraints;

/**
 * Single AZ step: upgrade this AZ in order; optionally pause after its tservers are upgraded. Used
 * for canary upgrade configuration. Preview API since 2026.1.0.0-b0.
 */
@ApiModel(
    description =
        "AZ upgrade step: order is list order; pauseAfterTserverUpgrade controls canary pause"
            + " after this AZ")
public class AZUpgradeStep {

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Availability zone UUID",
      example = "az-1-uuid",
      required = true)
  @Constraints.Required
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  public UUID azUUID;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. If true, upgrade pauses after all"
              + " tservers in this AZ are upgraded",
      example = "true")
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0.0-b0")
  public boolean pauseAfterTserverUpgrade = false;
}
