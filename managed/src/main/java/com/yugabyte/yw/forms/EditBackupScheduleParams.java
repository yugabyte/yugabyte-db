package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.backuprestore.annotations.AllowedScheduleState;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

@ApiModel(description = "Info to edit the schedule params for backups")
public class EditBackupScheduleParams {

  @ApiModelProperty(value = "State of the schedule")
  @AllowedScheduleState(anyOf = {State.Active, State.Stopped})
  public State status;

  @ApiModelProperty(value = "Frequency of the schedule")
  public Long frequency;

  // Specifies the time in millisecs before deleting the backup from the storage
  // bucket.
  @ApiModelProperty(value = "Time before deleting the backup from storage, in milliseconds")
  public long timeBeforeDelete = 0L;

  @ApiModelProperty(value = "Cron expression for scheduling")
  public String cronExpression;

  @ApiModelProperty(value = "Time Unit for frequency")
  public TimeUnit frequencyTimeUnit;

  @ApiModelProperty(value = "Frequency of incremental backup schedule")
  public Long incrementalBackupFrequency;

  @ApiModelProperty(value = "TimeUnit for incremental Backup Schedule frequency")
  public TimeUnit incrementalBackupFrequencyTimeUnit;

  // Run a full backup if the it was missed due to the schedule being paused. When false (default),
  // the full backup will instead run at its normally scheduled time.
  @ApiModelProperty(
      value =
          "Run a full or incremental backup if the schedule is expired when resuming a stopped"
              + " schedule")
  public boolean runImmediateBackupOnResume = false;
}
