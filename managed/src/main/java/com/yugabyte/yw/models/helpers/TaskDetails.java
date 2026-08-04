// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
/** Details for {@link com.yugabyte.yw.models.TaskInfo.class} */
public class TaskDetails {
  private long queuedTimeMs = -1;
  private long executionTimeMs = -1;
  private long totalTimeMs = -1;
  private int taskVersion;
  private String ybaVersion;
  private YBAError error;
  private JsonNode runtimeInfo;
}
