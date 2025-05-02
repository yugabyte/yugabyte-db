// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.backuprestore;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.backuprestore.annotations.AllowedScheduleState;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;

public class BackupScheduleToggleParams {
  @ApiModelProperty(value = "State of the schedule")
  @AllowedScheduleState(anyOf = {State.Active, State.Stopped})
  @NotNull
  public State status;

  @ApiModelProperty(
      value =
          "Run a full or incremental backup if required when resuming a stopped schedule. When"
              + " false (default), the full backup will instead run at its normally scheduled"
              + " time.")
  public boolean runImmediateBackupOnResume = false;

  public void verifyScheduleToggle(Schedule.State currentScheduleState) {
    if (currentScheduleState == this.status) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Schedule is already in %s state.", currentScheduleState));
    } else if (this.status == Schedule.State.Stopped
        && currentScheduleState != Schedule.State.Active) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Invalid state transition from %s to %s", currentScheduleState, this.status));
    } else if (this.status == Schedule.State.Active
        && currentScheduleState != Schedule.State.Stopped) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Invalid state transition from %s to %s", currentScheduleState, this.status));
    }
  }
}
