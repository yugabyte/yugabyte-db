// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import play.data.validation.Constraints.Required;

@ApiModel(description = "dr config set databases form")
public class DrConfigSetDatabasesForm {
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Source universe database IDs",
      example = "[\"000033df000030008000000000004006\", \"000033df00003000800000000000400b\"]")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  @Required
  public Set<String> databases;
}
