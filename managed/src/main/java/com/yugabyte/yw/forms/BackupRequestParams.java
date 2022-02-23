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
public class BackupRequestParams extends UniverseTaskParams {

  @Constraints.Required
  @ApiModelProperty(value = "Storage configuration UUID", required = true)
  public UUID storageConfigUUID;

  @ApiModelProperty(value = "KMS configuration UUID")
  public UUID kmsConfigUUID = null;

  @Constraints.Required
  @ApiModelProperty(value = "Universe UUID", required = true)
  public UUID universeUUID = null;

  @ApiModelProperty(value = "Backup type")
  public TableType backupType;

  // Specifies the time in millisecs before deleting the backup from the storage
  // bucket.
  @ApiModelProperty(value = "Time before deleting the backup from storage, in milliseconds")
  public long timeBeforeDelete = 0L;

  // Should backup script enable verbose logging.
  @ApiModelProperty(value = "Is verbose logging enabled")
  public boolean enableVerboseLogs = false;

  @ApiModelProperty(value = "Is SSE")
  public boolean sse = false;

  @ApiModelProperty(value = "Backup info")
  public List<KeyspaceTable> keyspaceTableList;

  // The number of concurrent commands to run on nodes over SSH
  @ApiModelProperty(value = "Number of concurrent commands to run on nodes over SSH")
  public int parallelism = 8;

  @ApiModelProperty(value = "Customer UUID")
  public UUID customerUUID = null;

  @ApiModelProperty(value = "Should table backup errors be ignored")
  public Boolean ignoreErrors = false;

  @ApiModelProperty(value = "Alter load balancer state")
  public boolean alterLoadBalancer = false;

  // Specifies the frequency for running the backup in milliseconds.
  @ApiModelProperty(value = "Frequency to run the backup, in milliseconds")
  public long schedulingFrequency = 0L;

  // Specifies the cron expression in case a recurring backup is expected.
  @ApiModelProperty(value = "Cron expression for a recurring backup")
  public String cronExpression = null;

  @ApiModelProperty(value = "Is tablespaces information included")
  public Boolean useTablespaces = false;

  // The associated schedule UUID (if applicable)
  @ApiModelProperty(value = "Schedule UUID")
  public UUID scheduleUUID = null;

  // Specifies number of backups to retain in case of recurring backups.
  @ApiModelProperty(value = "Minimum number of backups to retain for a particular backup schedule")
  public int minNumBackupsToRetain = Util.MIN_NUM_BACKUPS_TO_RETAIN;

  @ApiModel(description = "Keyspace and table info for backup")
  public static class KeyspaceTable {
    @ApiModelProperty(value = "Tables")
    public List<String> tableNameList;

    @ApiModelProperty(value = "Table UUIDs")
    public List<UUID> tableUUIDList;

    @ApiModelProperty(value = "keyspace")
    public String keyspace;
  }
}
