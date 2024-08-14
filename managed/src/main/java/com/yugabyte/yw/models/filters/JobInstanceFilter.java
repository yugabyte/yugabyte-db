// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.filters;

import com.yugabyte.yw.models.JobInstance.State;
import lombok.Builder;
import lombok.Getter;

@Builder
@Getter
public class JobInstanceFilter {
  private State state;
  private long startWindowSecs;
}
