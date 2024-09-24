// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
/** Details for {@link com.yugabyte.yw.models.TaskInfo.class} */
public class TaskDetails {
  private YBAError error;
}
