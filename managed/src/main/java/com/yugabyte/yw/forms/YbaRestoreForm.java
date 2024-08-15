// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints.Required;

@ApiModel(description = "isolated restore backup form")
public class YbaRestoreForm {

  @Required
  @ApiModelProperty(value = "Local filepath of backup.tar.gz", required = true)
  public String localPath;
}
