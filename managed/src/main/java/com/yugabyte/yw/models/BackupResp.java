package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import org.yb.CommonTypes.TableType;

@Value
@Builder
public class BackupResp {
  @ApiModelProperty(value = "The expiry time for backup.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date expiryTime;

  @ApiModelProperty(value = "Time for last incremenatal backup.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date lastIncrementalBackupTime;

  Long fullChainSizeInBytes;
  TimeUnit expiryTimeUnit;
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

  @JsonProperty("isFullBackup")
  Boolean isFullBackup;

  TableType backupType;
  CommonBackupInfo commonBackupInfo;
  String scheduleName;
  Boolean useTablespaces;
}
