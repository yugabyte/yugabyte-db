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
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import java.time.Duration;
import java.util.Objects;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class WaitForDuration extends UniverseTaskBase {

  @Inject
  protected WaitForDuration(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    // The universe UUID must be stored in universeUUID field.

    // The wait time before returning from the subtask.
    public Duration waitTime;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(waitTimeMs=%s)",
        super.getName(),
        Objects.nonNull(taskParams().waitTime) ? taskParams().waitTime.toMillis() : null);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    if (Objects.isNull(taskParams().waitTime)) {
      throw new IllegalArgumentException("taskParams().waitTime cannot be null");
    }

    if (taskParams().waitTime.compareTo(Duration.ZERO) <= 0) {
      log.info("wait time is less than or equal to 0; Skipping {}", getName());
      return;
    }

    try {
      log.debug("Sleeping for {}ms", taskParams().waitTime.toMillis());
      waitFor(taskParams().waitTime);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
