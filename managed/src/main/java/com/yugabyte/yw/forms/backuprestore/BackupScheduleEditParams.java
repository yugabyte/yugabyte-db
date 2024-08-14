// Copyright (c) YugaByte, Inc.

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

  @ApiModelProperty(value = "TimeUnit for incremental Backup Schedule frequency")
  public TimeUnit incrementalBackupFrequencyTimeUnit;

  // Used in testing only
  public BackupScheduleEditParams(BackupRequestParams params) {
    this.cronExpression = params.cronExpression;
    this.frequencyTimeUnit = params.frequencyTimeUnit;
    this.schedulingFrequency = params.schedulingFrequency;
    this.incrementalBackupFrequency = params.incrementalBackupFrequency;
    this.incrementalBackupFrequencyTimeUnit = params.incrementalBackupFrequencyTimeUnit;
  }
}
