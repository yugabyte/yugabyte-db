package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.Schedule.State;
import io.swagger.annotations.ApiModelProperty;

public class EditAccessKeyRotationScheduleParams {
  @ApiModelProperty(value = "State of the schedule")
  public State status = State.Active;

  @ApiModelProperty(value = "Frequency of the schedule in days")
  public int schedulingFrequencyDays;
}
