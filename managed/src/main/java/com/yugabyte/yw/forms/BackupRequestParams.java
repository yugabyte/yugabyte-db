// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import org.yb.CommonTypes.TableType;
import play.data.validation.Constraints;

@ApiModel(description = "Backup table parameters")
@NoArgsConstructor
@JsonIgnoreProperties({"currentYbcTaskId", "currentIdx", "backupDBStates"})
public class BackupRequestParams extends UniverseTaskParams {

  @Constraints.Required
  @ApiModelProperty(value = "Storage configuration UUID", required = true)
  public UUID storageConfigUUID;

  @ApiModelProperty(value = "KMS configuration UUID")
  public UUID kmsConfigUUID = null;

  @Constraints.Required
  @ApiModelProperty(value = "Universe UUID", required = true)
  @Getter
  @Setter
  private UUID universeUUID = null;

  @Constraints.Required
  @ApiModelProperty(
      value = "Backup type",
      allowableValues = "PGSQL_TABLE_TYPE, YQL_TABLE_TYPE, REDIS_TABLE_TYPE")
  public TableType backupType;

  // Specifies the time in millisecs before deleting the backup from the storage
  // bucket.
  @ApiModelProperty(value = "Time before deleting the backup from storage, in milliseconds")
  public long timeBeforeDelete = 0L;

  @ApiModelProperty(value = "Time unit for user input schedule frequency")
  public TimeUnit frequencyTimeUnit;

  // Should backup script enable verbose logging.
  @ApiModelProperty(value = "Is verbose logging enabled")
  public boolean enableVerboseLogs = false;

  @ApiModelProperty(value = "Is SSE")
  public boolean sse = false;

  @ApiModelProperty(value = "Disable checksum")
  public Boolean disableChecksum = false;

  @ApiModelProperty(value = "Disable multipart upload")
  public boolean disableMultipart = false;

  @ApiModelProperty(value = "Backup info")
  @JsonFormat(with = JsonFormat.Feature.ACCEPT_SINGLE_VALUE_AS_ARRAY)
  public List<KeyspaceTable> keyspaceTableList;

  // The number of concurrent commands to run on nodes over SSH
  @ApiModelProperty(value = "Number of concurrent commands to run on nodes over SSH")
  public int parallelism = 8;

  @ApiModelProperty(value = "Don't add -m flag during gsutil upload dir command")
  public boolean disableParallelism = false;

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

  @ApiModelProperty(value = "UUID of the parent backup")
  public UUID baseBackupUUID = null;

  @ApiModelProperty(value = "Frequency of incremental backups")
  public long incrementalBackupFrequency = 0L;

  @ApiModelProperty(value = "Time unit for user input incremental backup schedule frequency")
  public TimeUnit incrementalBackupFrequencyTimeUnit;

  // The associated schedule UUID (if applicable)
  @ApiModelProperty(value = "Schedule UUID")
  public UUID scheduleUUID = null;

  // The associated schedule name (if applicable)
  @ApiModelProperty(value = "Schedule Name")
  public String scheduleName = null;

  @ApiModelProperty(value = "Take table by table backups")
  public boolean tableByTableBackup = false;

  // Specifies number of backups to retain in case of recurring backups.
  @ApiModelProperty(value = "Minimum number of backups to retain for a particular backup schedule")
  public int minNumBackupsToRetain = Util.MIN_NUM_BACKUPS_TO_RETAIN;

  @ApiModelProperty(value = "Time unit for backup expiry time")
  public TimeUnit expiryTimeUnit;

  @ApiModelProperty(value = "Parallel DB backups")
  public int parallelDBBackups = 1;

  // Intermediate states to resume ybc backups
  public UUID backupUUID;

  // This param precedes in value even if YBC is installed and enabled on the universe.
  // If null, proceeds with usual behaviour.
  @ApiModelProperty(value = "Overrides whether you want to use YBC based or script based backup.")
  public BackupCategory backupCategory = null;

  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;

  public BackupRequestParams(BackupRequestParams backupRequestParams) {
    this.storageConfigUUID = backupRequestParams.storageConfigUUID;
    this.kmsConfigUUID = backupRequestParams.kmsConfigUUID;
    this.setUniverseUUID(backupRequestParams.getUniverseUUID());
    this.backupType = backupRequestParams.backupType;
    this.timeBeforeDelete = backupRequestParams.timeBeforeDelete;
    this.frequencyTimeUnit = backupRequestParams.frequencyTimeUnit;
    this.enableVerboseLogs = backupRequestParams.enableVerboseLogs;
    this.sse = backupRequestParams.sse;
    this.disableChecksum = backupRequestParams.disableChecksum;
    this.parallelism = backupRequestParams.parallelism;
    this.disableParallelism = backupRequestParams.disableParallelism;
    this.customerUUID = backupRequestParams.customerUUID;
    this.ignoreErrors = backupRequestParams.ignoreErrors;
    this.alterLoadBalancer = backupRequestParams.alterLoadBalancer;
    this.schedulingFrequency = backupRequestParams.schedulingFrequency;
    this.cronExpression = backupRequestParams.cronExpression;
    this.useTablespaces = backupRequestParams.useTablespaces;
    this.scheduleUUID = backupRequestParams.scheduleUUID;
    this.scheduleName = backupRequestParams.scheduleName;
    this.minNumBackupsToRetain = backupRequestParams.minNumBackupsToRetain;
    this.expiryTimeUnit = backupRequestParams.expiryTimeUnit;
    this.baseBackupUUID = backupRequestParams.baseBackupUUID;
    this.parallelDBBackups = backupRequestParams.parallelDBBackups;
    this.incrementalBackupFrequency = backupRequestParams.incrementalBackupFrequency;
    this.incrementalBackupFrequencyTimeUnit =
        backupRequestParams.incrementalBackupFrequencyTimeUnit;

    // Deep copy.
    if (backupRequestParams.keyspaceTableList == null) {
      this.keyspaceTableList = null;
    } else {
      this.keyspaceTableList = new ArrayList<>();
      for (KeyspaceTable keyspaceTable : backupRequestParams.keyspaceTableList) {
        KeyspaceTable copyKeyspaceTable = new KeyspaceTable();
        copyKeyspaceTable.keyspace = keyspaceTable.keyspace;
        if (keyspaceTable.tableNameList == null) {
          copyKeyspaceTable.tableNameList = null;
        } else {
          copyKeyspaceTable.tableNameList = new ArrayList<>(keyspaceTable.tableNameList);
        }
        if (keyspaceTable.tableUUIDList == null) {
          copyKeyspaceTable.tableUUIDList = null;
        } else {
          copyKeyspaceTable.tableUUIDList = new ArrayList<>(keyspaceTable.tableUUIDList);
        }
        this.keyspaceTableList.add(copyKeyspaceTable);
      }
    }
  }

  @ApiModel(description = "Keyspace and table info for backup")
  @ToString
  public static class KeyspaceTable {
    @ApiModelProperty(value = "Tables")
    public List<String> tableNameList = new ArrayList<>();

    @ApiModelProperty(value = "Table UUIDs")
    public List<UUID> tableUUIDList = new ArrayList<>();

    @ApiModelProperty(value = "keyspace")
    public String keyspace;
  }
}
