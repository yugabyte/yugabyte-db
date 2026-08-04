package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class CommonBackupInfo {
  BackupState state;
  UUID backupUUID;
  UUID baseBackupUUID;
  UUID storageConfigUUID;
  UUID kmsConfigUUID;
  UUID taskUUID;
  Boolean sse;
  Boolean tableByTableBackup;

  @ApiModelProperty(value = "Backup create time.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date createTime;

  @ApiModelProperty(value = "Backup update time.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date updateTime;

  @ApiModelProperty(value = "Backup completion time.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date completionTime;

  Long totalBackupSizeInBytes;
  Set<KeyspaceTablesList> responseList;
}
