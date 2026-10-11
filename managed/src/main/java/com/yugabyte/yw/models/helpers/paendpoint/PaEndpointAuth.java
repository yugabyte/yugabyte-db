// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.paendpoint;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import lombok.Data;

/** Credentials for one endpoint of a Perf Advisor Endpoint. */
@Data
@ApiModel(description = "Perf Advisor Endpoint credentials")
public class PaEndpointAuth {

  @NotNull
  @ApiModelProperty(value = "Authentication type")
  private PaEndpointAuthType type = PaEndpointAuthType.NONE;

  @ApiModelProperty(value = "Username, for basic authentication")
  private String username;

  @ApiModelProperty(value = "Password, for basic authentication")
  private String password;
}
