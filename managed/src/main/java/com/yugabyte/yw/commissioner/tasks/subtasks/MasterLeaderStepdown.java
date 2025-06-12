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

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.util.Objects;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.LeaderStepDownResponse;
import org.yb.client.YBClient;

@Slf4j
public class MasterLeaderStepdown extends UniverseTaskBase {

  private static final long WAIT_FOR_LEADER_STEPDOWN_TIMEOUT_MS = TimeUnit.MINUTES.toMillis(5);
  private static final long DELAY_BETWEEN_RETRIES = TimeUnit.SECONDS.toMillis(20);
  private static final long WAIT_FOR_MASTER_LEADER_TIMEOUT_MS = TimeUnit.MINUTES.toMillis(1);

  @Inject
  protected MasterLeaderStepdown(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String toString() {
    return getName();
  }

  @Override
  public void run() {
    // Get the master addresses.
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

    try (YBClient client = ybService.getUniverseClient(universe)) {
      String masterLeaderUUID = client.getLeaderMasterUUID();
      boolean result =
          doWithConstTimeout(
              DELAY_BETWEEN_RETRIES,
              WAIT_FOR_LEADER_STEPDOWN_TIMEOUT_MS,
              () -> {
                try {
                  LeaderStepDownResponse leaderStepDown = client.masterLeaderStepDown();
                  if (leaderStepDown.hasError()) {
                    log.error(
                        "Received {} message for leader stepdown", leaderStepDown.errorMessage());
                    return false;
                  }
                  client.waitForMasterLeader(WAIT_FOR_MASTER_LEADER_TIMEOUT_MS);
                  String newLeaderUUID = client.getLeaderMasterUUID();
                  return !Objects.equals(newLeaderUUID, masterLeaderUUID);
                } catch (Exception e) {
                  log.error("Received exception during stepdown", e);
                  return false;
                }
              });
      if (!result) {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Failed to do leader stepdown");
      }
    } catch (Exception e) {
      Throwables.propagate(e);
    }
  }
}
