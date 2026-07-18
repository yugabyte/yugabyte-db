// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@NoArgsConstructor
@AllArgsConstructor
@ApiModel("PA Collector universe registration status")
public class PaRegistrationStatusResponse {

  @ApiModelProperty(value = "Whether the universe is registered with PA Collector")
  private boolean success;

  @ApiModelProperty(
      value = "Whether advanced observability (metrics export to Prometheus) is enabled")
  private boolean advancedObservability;
}
