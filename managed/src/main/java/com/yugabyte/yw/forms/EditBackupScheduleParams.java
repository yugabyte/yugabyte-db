package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

@ApiModel(description = "Info to edit the schedule params for backups")
public class EditBackupScheduleParams {

  @ApiModelProperty(value = "State of the schedule")
  public State status = State.Active;

  @ApiModelProperty(value = "Frequency of the schedule")
  public Long frequency;

  @ApiModelProperty(value = "Cron expression for scheduling")
  public String cronExpression;

  @ApiModelProperty(value = "Time Unit for frequency")
  public TimeUnit frequencyTimeUnit;

  @ApiModelProperty(value = "Frequency of incremental backup schedule")
  public Long incrementalBackupFrequency;

  @ApiModelProperty(value = "TimeUnit for incremental Backup Schedule frequency")
  public TimeUnit incrementalBackupFrequencyTimeUnit;
}
