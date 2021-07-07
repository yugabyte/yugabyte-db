// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.Backup;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import org.yb.Common.TableType;
import play.data.validation.Constraints;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

@ApiModel(value = "Backup table params", description = "Backup table params")
public class BackupTableParams extends TableManagerParams {
  public enum ActionType {
    CREATE,
    RESTORE,
    RESTORE_KEYS,
    DELETE
  }

  @Constraints.Required
  @ApiModelProperty(value = "Storage config UUID", required = true)
  public UUID storageConfigUUID;

  @ApiModelProperty(value = "KMS config UUID")
  public UUID kmsConfigUUID = null;

  // Specifies the backup storage location in case of S3 it would have
  // the S3 url based on universeUUID and timestamp.
  @ApiModelProperty(value = "Storage location")
  public String storageLocation;

  @Constraints.Required
  @ApiModelProperty(value = "Action type", required = true)
  public ActionType actionType;

  @ApiModelProperty(value = "Backup type")
  public TableType backupType;

  @ApiModelProperty(value = "Tables")
  public List<String> tableNameList;

  @ApiModelProperty(value = "Tables UUID's")
  public List<UUID> tableUUIDList;

  // Allows bundling multiple backup params. Used only in the case
  // of backing up an entire universe transactionally
  @ApiModelProperty(value = "Backups")
  public List<BackupTableParams> backupList;

  // Specifies the frequency for running the backup in milliseconds.
  @ApiModelProperty(value = "Frequency for running the backup in milliseconds")
  public long schedulingFrequency = 0L;

  // Specifies the cron expression in case a recurring backup is expected.
  @ApiModelProperty(value = "Cron expression in case a recurring backup")
  public String cronExpression = null;

  // Specifies the time in millisecs before deleting the backup from the storage
  // bucket.
  @ApiModelProperty(value = "Time in millisecs before deleting the backup from the storage")
  public long timeBeforeDelete = 0L;

  // Should backup script enable verbose logging.
  @ApiModelProperty(value = "Is verbose logging is enable")
  public boolean enableVerboseLogs = false;

  // Should the backup be transactional across tables
  @ApiModelProperty(value = "Is backup be transactional across tables")
  public boolean transactionalBackup = false;

  // The number of concurrent commands to run on nodes over SSH
  @ApiModelProperty(value = "The number of concurrent commands to run on nodes over SSH")
  public int parallelism = 8;

  // The associated schedule UUID (if applicable)
  @ApiModelProperty(value = "Schedule UUID")
  public UUID scheduleUUID = null;

  @JsonIgnore public Backup backup = null;

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
