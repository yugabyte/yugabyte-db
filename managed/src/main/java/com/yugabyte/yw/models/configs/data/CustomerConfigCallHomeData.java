// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;

public class CustomerConfigCallHomeData extends CustomerConfigData {
  @ApiModelProperty(value = "CallHome level")
  @NotNull
  public CollectionLevel callhomeLevel;
}
