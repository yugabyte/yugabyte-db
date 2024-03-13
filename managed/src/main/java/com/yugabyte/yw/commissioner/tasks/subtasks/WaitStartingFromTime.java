/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import java.time.Duration;
import java.util.Date;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class WaitStartingFromTime extends UniverseTaskBase {

  @Inject
  protected WaitStartingFromTime(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public long sleepTime;
    public String dateKey; // We get the start of interval from tasks cache using this key.
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format("%s(sleepTime=%s)", super.getName(), taskParams().sleepTime);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Date startDate = getDateFromCache(taskParams().dateKey);
    if (startDate == null) {
      log.warn(
          "No start date in cache: keys: {} desired {}",
          getTaskCache().keys(),
          taskParams().dateKey);
      return;
    }
    long deadLine = startDate.getTime() + taskParams().sleepTime;
    long sleepTime = deadLine - System.currentTimeMillis();
    log.debug("Start {} deadline {}", startDate, new Date(deadLine));
    if (sleepTime <= 0) {
      log.debug("Already passed deadline ({}), skipping", new Date(deadLine));
      return;
    }

    try {
      log.debug("Sleeping for {}ms", sleepTime);
      waitFor(Duration.ofMillis(getSleepMultiplier() * sleepTime));
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
