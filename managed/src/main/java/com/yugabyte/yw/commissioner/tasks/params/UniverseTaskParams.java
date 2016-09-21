// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import java.util.UUID;

public class UniverseTaskParams implements ITaskParams{
  // The universe against which this node's details should be saved.
  public UUID universeUUID;
}
