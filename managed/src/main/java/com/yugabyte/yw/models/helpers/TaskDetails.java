// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
/** Details for {@link com.yugabyte.yw.models.TaskInfo.class} */
public class TaskDetails {
  private String version;
  private YBAError error;
  private JsonNode runtimeInfo;
}
