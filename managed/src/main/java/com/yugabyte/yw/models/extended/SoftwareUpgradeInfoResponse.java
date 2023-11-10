// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.extended;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Data;

@Data
@ApiModel(description = "Software Upgrade Info response")
public class SoftwareUpgradeInfoResponse {

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Finalize required",
      accessMode = AccessMode.READ_ONLY)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
  private boolean finalizeRequired;
}
