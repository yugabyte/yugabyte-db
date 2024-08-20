// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.UUID;

@ApiModel(description = "continuous backup config get response")
public class ContinuousBackupGetResp {

  private final ContinuousBackupConfig cbConfig;

  public ContinuousBackupGetResp(ContinuousBackupConfig cbConfig) {
    this.cbConfig = cbConfig;
  }

  @ApiModelProperty(value = "Continuous backup config UUID")
  public UUID getUuid() {
    return cbConfig.getUuid();
  }

  @ApiModelProperty(value = "Continuous backup config backup interval")
  public String getBackupInterval() {
    // TODO: construct from frequency + frequency time unit
    return "5 minutes";
  }

  @ApiModelProperty(value = "Continuous backup config location")
  public String getStorageLocation() {
    // TODO: construct this from storage config location + folder name
    return "s3://backup_storage_path/YBA_1.2.3.4";
  }

  @ApiModelProperty(
      value = "Last time a backup was made to storage location",
      example = "2024-07-28T01:02:03Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date getLastBackup() {
    // TODO: read from row entry that we make on every backup
    return new Date(2024, 8, 2);
  }
}
