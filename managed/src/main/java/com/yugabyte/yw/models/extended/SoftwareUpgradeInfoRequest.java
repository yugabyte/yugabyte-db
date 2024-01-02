// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.extended;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import play.data.validation.Constraints;

@Data
@ApiModel(description = "Software upgrade info request")
public class SoftwareUpgradeInfoRequest {

  @Constraints.Required
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. DB version",
      required = true)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.2.0")
  private String ybSoftwareVersion;
}
