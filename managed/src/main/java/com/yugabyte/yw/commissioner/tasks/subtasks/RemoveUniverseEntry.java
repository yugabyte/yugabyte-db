// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.models.Universe;

public class RemoveUniverseEntry extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(RemoveUniverseEntry.class);

  @Override
  public void run() {
    Universe.delete(taskParams().universeUUID);
  }
}
