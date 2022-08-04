package com.yugabyte.yw.models;

import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import org.yb.CommonTypes.TableType;

@Value
@Builder
public class BackupResp {

  BackupState state;
  TableType backupType;
  UUID backupUUID;
  UUID storageConfigUUID;
  UUID universeUUID;
  UUID scheduleUUID;
  UUID customerUUID;
  UUID kmsConfigUUID;
  UUID taskUUID;
  String universeName;
  Boolean isStorageConfigPresent;
  Boolean isUniversePresent;
  Boolean onDemand;
  Boolean sse;
  Boolean isFullBackup;
  Date createTime;
  Date updateTime;
  Date expiryTime;
  Date completionTime;
  Long totalBackupSizeInBytes;
  BackupCategory category;
  Set<KeyspaceTablesList> responseList;
  StorageConfigType storageConfigType;
}
