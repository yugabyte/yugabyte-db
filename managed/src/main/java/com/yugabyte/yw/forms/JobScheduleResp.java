// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.JobSchedule;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class JobScheduleResp {
  @JsonUnwrapped private final JobSchedule jobSchedule;

  @JsonCreator
  public JobScheduleResp(JobSchedule jobSchedule) {
    this.jobSchedule = jobSchedule;
  }
}
