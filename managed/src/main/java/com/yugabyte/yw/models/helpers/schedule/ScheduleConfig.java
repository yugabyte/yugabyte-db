// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers.schedule;

import com.fasterxml.jackson.annotation.JsonFormat;
import java.util.Date;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.extern.jackson.Jacksonized;

@Getter
@Builder(toBuilder = true)
@Jacksonized
@EqualsAndHashCode
public class ScheduleConfig {
  @Builder.Default private ScheduleType type = ScheduleType.FIXED_DELAY;

  @Builder.Default private long intervalSecs = 60;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date snoozeUntil;

  private boolean disabled;

  public enum ScheduleType {
    FIXED_DELAY,
    FIXED_RATE
  }
}
