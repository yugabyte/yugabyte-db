// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.helpers.TimeUnit;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import play.data.validation.Constraints.Required;

@ApiModel(description = "continuous backup config create form")
public class ContinuousBackupForm {

  @Required
  @ApiModelProperty(value = "Storage Config UUID", required = true)
  public UUID storageConfigUUID = null;

  @Required
  @ApiModelProperty(value = "wait between backups", required = true)
  public long frequency = 0L;

  @ApiModelProperty(value = "time unit for wait between backups")
  public TimeUnit frequencyTimeUnit = TimeUnit.MINUTES;

  @ApiModelProperty(value = "the number of previous backups to retain")
  public int numBackupsToRetain = 5;

  @Required
  @ApiModelProperty(
      value = "the folder in storage config to store backups for this YBA",
      required = true)
  public String backupDir;
}
