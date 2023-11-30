// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.forms.ybc.YbcGflags;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import play.data.validation.Constraints;

@NoArgsConstructor
public class YbcGflagsTaskParams extends UpgradeTaskParams {

  @Constraints.Required
  @ApiModelProperty(value = "Customer UUID")
  public UUID customerUUID;

  @Constraints.Required
  @ApiModelProperty(value = "Universe UUID", required = true)
  @Getter
  @Setter
  private UUID universeUUID;

  @ApiModelProperty public YbcGflags ybcGflags;
}
