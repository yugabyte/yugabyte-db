// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class CustomerConfigStorageNFSData extends CustomerConfigData {
  @ApiModelProperty(value = "Backup location", example = "/mnt/storage")
  @JsonProperty("BACKUP_LOCATION")
  @NotNull
  @Size(min = 1)
  public String backupLocation;
}
