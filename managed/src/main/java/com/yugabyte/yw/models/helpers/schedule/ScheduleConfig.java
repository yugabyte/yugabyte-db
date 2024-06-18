// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers.schedule;

import java.time.Duration;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.jackson.Jacksonized;

@Getter
@Builder(toBuilder = true)
@Jacksonized
public class ScheduleConfig {
  @Builder.Default private ScheduleType type = ScheduleType.FIXED_DELAY;
  @Builder.Default private Duration interval = Duration.ofMinutes(1);
  private boolean disabled;

  public enum ScheduleType {
    FIXED_DELAY,
    FIXED_RATE
  }
}
