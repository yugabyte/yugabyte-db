/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class MarkUniverseForHealthScriptReUpload extends UniverseTaskBase {

  private final HealthChecker healthChecker;

  @Inject
  protected MarkUniverseForHealthScriptReUpload(
      BaseTaskDependencies baseTaskDependencies, HealthChecker healthChecker) {
    super(baseTaskDependencies);
    this.healthChecker = healthChecker;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      healthChecker.markUniverseForReUpload(getUniverse().getUniverseUUID());
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
