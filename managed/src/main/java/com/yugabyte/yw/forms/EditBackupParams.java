package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

@ApiModel(description = "Edit backup parameters")
public class EditBackupParams {

  @ApiModelProperty(
      value = "Time before deleting the backup from storage, in milliseconds",
      required = true)
  public long timeBeforeDeleteFromPresentInMillis = 0L;
}
