package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonAlias;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import play.data.validation.Constraints;

public class DeleteBackupParams {

  @Constraints.Required
  @JsonAlias({"backups"})
  @ApiModelProperty(value = "Backups to be deleted", required = true)
  public List<DeleteBackupInfo> deleteBackupInfos;

  @ApiModelProperty(value = "Delete Backups forcefully", required = false)
  public boolean deleteForcefully = false;

  @ApiModel
  public static class DeleteBackupInfo {

    @Constraints.Required
    @ApiModelProperty(value = "backup UUID", required = true)
    public UUID backupUUID;

    @ApiModelProperty(value = "storage config UUID")
    public UUID storageConfigUUID;
  }
}
