// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class JobScheduleSnoozeForm {
  @Constraints.Required public long snoozeSecs;
}
