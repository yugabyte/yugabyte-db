package com.yugabyte.yw.models;

import java.util.Date;
import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;

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
  Date createTime;
  Date updateTime;
  Date completionTime;
  Long totalBackupSizeInBytes;
  Set<KeyspaceTablesList> responseList;
}
