// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.UUID;

public class TableTaskParams extends UniverseTaskParams {

  // Unique identifier for the table corresponding to this task
  public UUID tableUUID;
}
