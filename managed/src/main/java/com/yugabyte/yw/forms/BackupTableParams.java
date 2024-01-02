// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import com.yugabyte.yw.models.backuprestore.Tablespace;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import org.apache.commons.collections.CollectionUtils;
import org.yb.CommonTypes.TableType;
import play.data.validation.Constraints;

@ApiModel(description = "Backup table parameters")
@NoArgsConstructor
public class BackupTableParams extends TableManagerParams {
  public enum ActionType {
    CREATE,
    RESTORE,
    RESTORE_KEYS,
    DELETE
  }

  @Constraints.Required
  @ApiModelProperty(value = "Storage configuration UUID", required = true)
  public UUID storageConfigUUID;

  @ApiModelProperty(value = "KMS configuration UUID")
  public UUID kmsConfigUUID = null;

  // Specifies the backup storage location in case of S3 it would have
  // the S3 url based on universeUUID and timestamp.
  @ApiModelProperty(value = "Storage location")
  public String storageLocation;

  @ApiModelProperty(value = "Action type")
  public ActionType actionType;

  @ApiModelProperty(value = "Full Table type backup")
  public Boolean isFullBackup = false;

  @ApiModelProperty(value = "Backup all tables in Keyspace")
  public boolean allTables = false;

  @ApiModelProperty(value = "Disable checksum")
  public Boolean disableChecksum = false;

  @ApiModelProperty(value = "Disable multipart upload")
  public boolean disableMultipart = false;

  @ApiModelProperty(value = "Backup type")
  public TableType backupType;

  @ApiModelProperty(value = "Tables")
  public List<String> tableNameList;

  @ApiModelProperty(value = "Table UUIDs")
  public List<UUID> tableUUIDList;

  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private Map<String, Set<String>> tablesWithIndexesMap;

  // Allows bundling multiple backup params. Used only in the case
  // of backing up an entire universe transactionally
  @ApiModelProperty(value = "Backups")
  public List<BackupTableParams> backupList;

  @ApiModelProperty(hidden = true)
  public UUID backupParamsIdentifier = null;

  @ApiModelProperty(value = "Per region locations")
  public List<BackupUtil.RegionLocations> regionLocations;

  // Specifies the frequency for running the backup in milliseconds.
  @ApiModelProperty(value = "Frequency to run the backup, in milliseconds")
  public long schedulingFrequency = 0L;

  // Specifies the cron expression in case a recurring backup is expected.
  @ApiModelProperty(value = "Cron expression for a recurring backup")
  public String cronExpression = null;

  // Specifies number of backups to retain in case of recurring backups.
  @ApiModelProperty(value = "Minimum number of backups to retain for a particular backup schedule")
  public int minNumBackupsToRetain = Util.MIN_NUM_BACKUPS_TO_RETAIN;

  // Specifies the time in millisecs before deleting the backup from the storage
  // bucket.
  @ApiModelProperty(value = "Time before deleting the backup from storage, in milliseconds")
  public long timeBeforeDelete = 0L;

  // Should backup script enable verbose logging.
  @ApiModelProperty(value = "Is verbose logging enabled")
  public boolean enableVerboseLogs = false;

  @ApiModelProperty(value = "Alter load balancer state")
  public boolean alterLoadBalancer = false;

  // Should the backup be transactional across tables
  @ApiModelProperty(value = "Is backup transactional across tables")
  @Deprecated
  public boolean transactionalBackup = false;

  @ApiModelProperty(value = "Table by table backup")
  public boolean tableByTableBackup = false;

  // The number of concurrent commands to run on nodes over SSH
  @ApiModelProperty(value = "Number of concurrent commands to run on nodes over SSH")
  public int parallelism = 8;

  @ApiModelProperty(value = "Don't add -m flag during gsutil upload dir command")
  public boolean disableParallelism = false;

  // The associated schedule UUID (if applicable)
  @ApiModelProperty(value = "Schedule UUID")
  public UUID scheduleUUID = null;

  @ApiModelProperty(value = "Customer UUID")
  public UUID customerUuid = null;

  @ApiModelProperty(value = "Base backup UUID")
  public UUID baseBackupUUID = null;

  @ApiModelProperty(value = "Backup UUID")
  public UUID backupUuid = null;

  @ApiModelProperty(value = "Schedule Name")
  public String scheduleName = null;

  @ApiModelProperty(value = "Should table backup errors be ignored")
  public Boolean ignoreErrors = false;

  @ApiModelProperty(value = "Restore TimeStamp")
  public String restoreTimeStamp = null;

  @ApiModelProperty(value = "Is tablespaces information included")
  public Boolean useTablespaces = false;

  @ApiModelProperty(value = "User name of the current tables owner")
  public String oldOwner = "yugabyte";

  @ApiModelProperty(value = "User name of the new tables owner")
  public String newOwner = null;

  @ApiModelProperty(value = "Backup size in bytes")
  public long backupSizeInBytes = 0L;

  @ApiModelProperty(value = "Incremental backups chain size")
  public long fullChainSizeInBytes = 0L;

  @ApiModelProperty(value = "Type of backup storage config")
  public StorageConfigType storageConfigType = null;

  @ApiModelProperty(value = "Time unit for backup expiry time")
  public TimeUnit expiryTimeUnit = TimeUnit.DAYS;

  @ApiModelProperty(value = "Tablespaces info")
  @Getter
  @Setter
  private List<Tablespace> tablespacesList = null;

  // For each list item
  public long timeTakenPartial = 0L;

  @ApiModelProperty(hidden = true)
  public long thisBackupSubTaskStartTime = 0L;

  @ApiModelProperty(hidden = true)
  public final Map<UUID, ParallelBackupState> backupDBStates = new ConcurrentHashMap<>();

  @ToString
  public static class ParallelBackupState {
    public String nodeIp;
    public String currentYbcTaskId;
    public boolean alreadyScheduled = false;

    public void resetOnComplete() {
      this.nodeIp = null;
      this.currentYbcTaskId = null;
      this.alreadyScheduled = true;
    }

    public void setIntermediate(String nodeIp, String currentYbcTaskId) {
      this.nodeIp = nodeIp;
      this.currentYbcTaskId = currentYbcTaskId;
    }
  }

  @JsonIgnore
  public void initializeBackupDBStates() {
    this.backupList
        .parallelStream()
        .forEach(
            paramsEntry ->
                this.backupDBStates.put(
                    paramsEntry.backupParamsIdentifier, new ParallelBackupState()));
  }

  @JsonIgnore
  public BackupTableParams(BackupRequestParams backupRequestParams) {
    this.customerUuid = backupRequestParams.customerUUID;
    // Todo: Should it always be set to true?
    this.ignoreErrors = true;
    //    this.ignoreErrors = backupRequestParams.ignoreErrors;
    this.storageConfigUUID = backupRequestParams.storageConfigUUID;
    this.setUniverseUUID(backupRequestParams.getUniverseUUID());
    this.sse = backupRequestParams.sse;
    this.parallelism = backupRequestParams.parallelism;
    this.timeBeforeDelete = backupRequestParams.timeBeforeDelete;
    this.expiryTimeUnit = backupRequestParams.expiryTimeUnit;
    this.backupType = backupRequestParams.backupType;
    this.isFullBackup = CollectionUtils.isEmpty(backupRequestParams.keyspaceTableList);
    this.scheduleUUID = backupRequestParams.scheduleUUID;
    this.scheduleName = backupRequestParams.scheduleName;
    this.disableChecksum = backupRequestParams.disableChecksum;
    this.disableMultipart = backupRequestParams.disableMultipart;
    this.useTablespaces = backupRequestParams.useTablespaces;
    this.disableParallelism = backupRequestParams.disableParallelism;
    this.baseBackupUUID = backupRequestParams.baseBackupUUID;
    this.enableVerboseLogs = backupRequestParams.enableVerboseLogs;
  }

  @JsonIgnore
  public BackupTableParams(BackupRequestParams backupRequestParams, String keySpace) {
    this(backupRequestParams);
    this.setKeyspace(keySpace);
    this.tableNameList = new ArrayList<>();
    this.tableUUIDList = new ArrayList<>();
    this.setTableName(null);
    this.tableUUID = null;
  }

  @JsonIgnore
  public BackupTableParams(BackupTableParams tableParams) {
    this.customerUuid = tableParams.customerUuid;
    this.backupUuid = tableParams.backupUuid;
    this.ignoreErrors = true;
    this.storageConfigUUID = tableParams.storageConfigUUID;
    this.storageLocation = tableParams.storageLocation;
    this.storageConfigType = tableParams.storageConfigType;
    this.setUniverseUUID(tableParams.getUniverseUUID());
    this.sse = tableParams.sse;
    this.parallelism = tableParams.parallelism;
    this.timeBeforeDelete = tableParams.timeBeforeDelete;
    this.expiryTimeUnit = tableParams.expiryTimeUnit;
    this.backupType = tableParams.backupType;
    this.isFullBackup = tableParams.isFullBackup;
    this.allTables = tableParams.allTables;
    this.scheduleUUID = tableParams.scheduleUUID;
    this.scheduleName = tableParams.scheduleName;
    this.disableChecksum = tableParams.disableChecksum;
    this.useTablespaces = tableParams.useTablespaces;
    this.disableParallelism = tableParams.disableParallelism;
    this.enableVerboseLogs = tableParams.enableVerboseLogs;
    this.disableMultipart = tableParams.disableMultipart;
    this.baseBackupUUID = tableParams.baseBackupUUID;
    this.setKeyspace(tableParams.getKeyspace());
    this.tableNameList = new ArrayList<>(tableParams.getTableNameList());
    this.tableUUIDList = new ArrayList<>(tableParams.getTableUUIDList());
    this.setTableName(tableParams.getTableName());
    this.tableUUID = tableParams.tableUUID;
    this.backupParamsIdentifier = tableParams.backupParamsIdentifier;
  }

  @JsonIgnore
  public BackupTableParams(BackupTableParams tableParams, UUID tableUUID, String tableName) {
    this(tableParams);
    this.tableUUIDList = Arrays.asList(tableUUID);
    this.tableNameList = Arrays.asList(tableName);
    this.setTableName(tableName);
  }

  @JsonIgnore
  public Set<String> getTableNames() {
    Set<String> tableNames = new HashSet<>();
    if (tableUUIDList != null && !tableUUIDList.isEmpty()) {
      if (tableNameList != null) {
        tableNames.addAll(tableNameList);
      }
    } else if (getTableName() != null) {
      tableNames.add(getTableName());
    }

    return tableNames;
  }

  public List<UUID> getTableUUIDList() {
    if (tableUUIDList != null) {
      return tableUUIDList;
    } else if (tableUUID != null) {
      return Arrays.asList(tableUUID);
    }
    return new ArrayList<UUID>();
  }

  public List<String> getTableNameList() {
    if (tableNameList != null) {
      return tableNameList;
    } else if (getTableName() != null) {
      return Arrays.asList(getTableName());
    }
    return new ArrayList<String>();
  }

  public boolean isFullBackup() {
    return isFullBackup;
  }

  public void setFullBackup(boolean isFullBackup) {
    this.isFullBackup = isFullBackup;
  }

  @JsonIgnore public List<UUID> targetAsyncReplicationRelationships;

  @JsonIgnore public List<UUID> sourceAsyncReplicationRelationships;
}
