// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class CustomerConfigStorageData extends CustomerConfigData {
  @ApiModelProperty(value = "Backup location", example = "s3://backups.yugabyte.com/test/guest")
  @JsonProperty("BACKUP_LOCATION")
  @NotNull
  @Size(min = 5)
  public String backupLocation;
}
