// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig.ScheduleType;
import play.data.validation.Constraints;

public class JobScheduleUpdateForm {
  @Constraints.Required public ScheduleType type;
  @Constraints.Required public long intervalSecs;
  public boolean disabled;
}
