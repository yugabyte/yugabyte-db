// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.filters;

import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig.ScheduleType;
import lombok.Builder;
import lombok.Getter;

@Builder
@Getter
public class JobScheduleFilter {
  private String nameRegex;
  private String configClass;
  private ScheduleType type;
  private long nextStartWindowSecs;
  private boolean enabledOnly;
}
