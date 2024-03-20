// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import javax.validation.Valid;
import lombok.ToString;
import play.data.validation.Constraints.Required;

@ApiModel(description = "dr config restart form")
@ToString
public class DrConfigRestartForm {

  @ApiModelProperty(
      value = "Primary Universe DB IDs",
      example = "[\"0000412b000030008000000000000000\", \"0000412b000030008000000000000001\"]",
      required = true)
  @Required
  public Set<String> dbs;

  @Valid
  @ApiModelProperty("Parameters needed for the bootstrap flow including backup/restore")
  public XClusterConfigRestartFormData.RestartBootstrapParams bootstrapParams;
}
