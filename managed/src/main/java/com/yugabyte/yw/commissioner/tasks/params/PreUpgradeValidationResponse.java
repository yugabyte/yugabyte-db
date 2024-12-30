// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collections;
import java.util.List;
import lombok.Data;

@Data
@ApiModel(description = "Upgrade Validation Response")
public class PreUpgradeValidationResponse {

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "Indicates whether all the checks passed",
      accessMode = ApiModelProperty.AccessMode.READ_ONLY)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.2.1")
  private boolean success;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "List of errors that occurred during validation",
      accessMode = ApiModelProperty.AccessMode.READ_ONLY)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.2.1")
  private List<String> errors;

  public static PreUpgradeValidationResponse fromError(String error) {
    PreUpgradeValidationResponse response = new PreUpgradeValidationResponse();
    response.setErrors(Collections.singletonList(error));
    return response;
  }
}
