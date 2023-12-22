package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.models.Backup.BackupCategory;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import lombok.Builder;
import lombok.Data;

@Data
@Builder(toBuilder = true)
public class RestorePreflightResponse {

  @ApiModelProperty(value = "Whether backup was KMS encrypted")
  @Builder.Default
  private Boolean hasKMSHistory = false;

  @ApiModelProperty(value = "Backup Category")
  @Builder.Default
  private BackupCategory backupCategory = BackupCategory.YB_CONTROLLER;

  @ApiModelProperty(value = "Map of backup location and backup-info object")
  private Map<String, BackupUtil.PerLocationBackupInfo> perLocationBackupInfoMap;

  @ApiModelProperty(hidden = true)
  @JsonIgnore
  @Builder.Default
  private Map<String, YbcBackupResponse> successMarkerMap = null;

  @ApiModelProperty(hidden = true)
  @Builder.Default
  private String loggingID = null;
}
