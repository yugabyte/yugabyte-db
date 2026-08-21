// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.backuprestore;

import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.swagger.annotations.ApiModelProperty;
import lombok.NoArgsConstructor;

@NoArgsConstructor
public class BackupScheduleEditParams {
  @ApiModelProperty(value = "Frequency of the schedule")
  public long schedulingFrequency;

  @ApiModelProperty(value = "Cron expression for scheduling")
  public String cronExpression;

  @ApiModelProperty(value = "Time Unit for frequency")
  public TimeUnit frequencyTimeUnit;

  @ApiModelProperty(value = "Frequency of incremental backup schedule")
  public long incrementalBackupFrequency;

  // Specifies the time in millisecs before deleting the backup from the storage
  // bucket.
  @ApiModelProperty(value = "Time before deleting the backup from storage, in milliseconds")
  public long timeBeforeDelete = 0L;

  @ApiModelProperty(value = "TimeUnit for incremental Backup Schedule frequency")
  public TimeUnit incrementalBackupFrequencyTimeUnit;

  @ApiModelProperty(value = "Is tablespaces information included")
  public Boolean useTablespaces;

  @ApiModelProperty(value = "Backup global ysql roles")
  public Boolean useRoles;

  @ApiModelProperty(value = "Backup privileges for roles")
  public Boolean usePrivileges;

  // Used in testing/operator
  public BackupScheduleEditParams(BackupRequestParams params) {
    this.cronExpression = params.cronExpression;
    this.frequencyTimeUnit = params.frequencyTimeUnit;
    this.schedulingFrequency = params.schedulingFrequency;
    this.incrementalBackupFrequency = params.incrementalBackupFrequency;
    this.incrementalBackupFrequencyTimeUnit = params.incrementalBackupFrequencyTimeUnit;
    this.timeBeforeDelete = params.timeBeforeDelete;
    this.useTablespaces = params.useTablespaces;
    this.useRoles = params.getUseRoles();
    this.usePrivileges = params.getUsePrivileges();
  }
}
