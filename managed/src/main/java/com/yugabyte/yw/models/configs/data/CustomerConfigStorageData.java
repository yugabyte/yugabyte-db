// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.configs.validators.BackupLocationLengthConstraint;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;

public class CustomerConfigStorageData extends CustomerConfigData {
  @ApiModelProperty(value = "Backup location", example = "s3://backups.yugabyte.com/test/guest")
  @JsonProperty("BACKUP_LOCATION")
  @NotNull
  @BackupLocationLengthConstraint
  public String backupLocation;
}
