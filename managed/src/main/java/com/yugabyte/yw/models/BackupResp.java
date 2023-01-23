package com.yugabyte.yw.models;

import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import java.util.Date;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import org.yb.CommonTypes.TableType;

@Value
@Builder
public class BackupResp {
  Date expiryTime;
  Date lastIncrementalBackupTime;
  Long fullChainSizeInBytes;
  String universeName;
  Boolean isStorageConfigPresent;
  Boolean isUniversePresent;
  UUID universeUUID;
  UUID scheduleUUID;
  UUID customerUUID;
  Boolean hasIncrementalBackups;
  BackupState lastBackupState;
  Boolean onDemand;
  StorageConfigType storageConfigType;
  BackupCategory category;
  Boolean isFullBackup;
  TableType backupType;
  CommonBackupInfo commonBackupInfo;
}
