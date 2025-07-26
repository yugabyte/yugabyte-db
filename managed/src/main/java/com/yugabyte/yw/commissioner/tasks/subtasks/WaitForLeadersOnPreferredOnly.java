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

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class WaitForLeadersOnPreferredOnly extends AbstractTaskBase {

  // Timeout for failing to complete load balance. Currently we do no timeout.
  // NOTE: This is similar to WaitForDataMove for blacklist removal.
  private static final long TIMEOUT_SERVER_WAIT_MS = Long.MAX_VALUE;

  @Inject
  protected WaitForLeadersOnPreferredOnly(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for data move wait task.
  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    boolean ret;
    try (YBClient client = ybService.getUniverseClient(universe)) {
      log.info("Running {}: masterAddresses={}.", getName(), universe.getMasterAddresses());

      ret = client.waitForAreLeadersOnPreferredOnlyCondition(TIMEOUT_SERVER_WAIT_MS);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    if (!ret) {
      throw new RuntimeException(getName() + " did not complete.");
    }
  }
}
