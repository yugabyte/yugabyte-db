/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;

public class RemoveUniverseEntry extends UniverseTaskBase {

  private final HealthChecker healthChecker;

  @Inject
  protected RemoveUniverseEntry(
      BaseTaskDependencies baseTaskDependencies, HealthChecker healthChecker) {
    super(baseTaskDependencies);
    this.healthChecker = healthChecker;
  }

  @Override
  protected DestroyUniverse.Params taskParams() {
    return (DestroyUniverse.Params) taskParams;
  }

  @Override
  public void run() {
    Universe.delete(taskParams().getUniverseUUID());

    alertConfigurationService.handleSourceRemoval(
        taskParams().customerUUID,
        AlertConfiguration.TargetType.UNIVERSE,
        taskParams().getUniverseUUID());
    metricService.markSourceRemoved(taskParams().customerUUID, taskParams().getUniverseUUID());
    healthChecker.handleUniverseRemoval(taskParams().getUniverseUUID());
  }
}
