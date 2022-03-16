/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckMasterLeader extends UniverseDefinitionTaskBase {

  @Inject
  protected CheckMasterLeader(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    try {
      // Wait for new masters to be responsive.
      createWaitForMasterLeaderTask();
      // Run the task.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    }
  }
}
