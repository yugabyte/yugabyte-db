// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import play.data.validation.Constraints.Required;

@ApiModel(description = "continuous backup restore form")
public class ContinuousRestoreForm {

  @Required
  @ApiModelProperty(value = "Storage Config UUID", required = true)
  public UUID storageConfigUUID = null;

  @Required
  @ApiModelProperty(value = "the folder in storage config to look for YBA backups", required = true)
  public String backupDir;
}
