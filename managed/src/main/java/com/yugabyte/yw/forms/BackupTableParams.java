// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.Util;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.yb.CommonTypes.TableType;
import play.data.validation.Constraints;

@ApiModel(description = "Backup table parameters")
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

  @ApiModelProperty(value = "Backup type")
  public TableType backupType;

  @ApiModelProperty(value = "Tables")
  public List<String> tableNameList;

  @ApiModelProperty(value = "Table UUIDs")
  public List<UUID> tableUUIDList;

  // Allows bundling multiple backup params. Used only in the case
  // of backing up an entire universe transactionally
  @ApiModelProperty(value = "Backups")
  public List<BackupTableParams> backupList;

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

  // Should the backup be transactional across tables
  @ApiModelProperty(value = "Is backup transactional across tables")
  public boolean transactionalBackup = false;

  // The number of concurrent commands to run on nodes over SSH
  @ApiModelProperty(value = "Number of concurrent commands to run on nodes over SSH")
  public int parallelism = 8;

  // The associated schedule UUID (if applicable)
  @ApiModelProperty(value = "Schedule UUID")
  public UUID scheduleUUID = null;

  @ApiModelProperty(value = "Customer UUID")
  public UUID customerUuid = null;

  @ApiModelProperty(value = "Backup UUID")
  public UUID backupUuid = null;

  @ApiModelProperty(value = "Controller type")
  public String controller = null;

  @ApiModelProperty(value = "Should table backup errors be ignored")
  public Boolean ignoreErrors = false;

  @ApiModelProperty(value = "Restore TimeStamp")
  public String restoreTimeStamp = null;

  @JsonIgnore
  public Set<String> getTableNames() {
    Set<String> tableNames = new HashSet<>();
    if (tableUUIDList != null && !tableUUIDList.isEmpty()) {
      tableNames.addAll(tableNameList);
    } else if (getTableName() != null) {
      tableNames.add(getTableName());
    }

    return tableNames;
  }
}
