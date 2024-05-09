// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.RollbackAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterTypes.MasterErrorPB;

@Slf4j
public class RollbackAutoFlags extends ServerSubTaskBase {

  @Inject
  protected RollbackAutoFlags(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {
    public int rollbackVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format("%s (universeUuid=%s)", super.getName(), taskParams().getUniverseUUID());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    try (YBClient client = getClient()) {
      RollbackAutoFlagsResponse resp = client.rollbackAutoFlags(taskParams().rollbackVersion);
      if (resp.hasError()) {
        MasterErrorPB error = resp.getError();
        log.error(error.toString());
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, error.toString());
      }
      Duration sleepTime =
          confGetter.getConfForScope(
              universe, UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds);
      log.info("waiting for {} ms after updating auto flags", sleepTime.toMillis());
      if (sleepTime.toMillis() > 0) {
        waitFor(sleepTime);
      }
    } catch (Exception e) {
      log.error("Rollback AutoFlag task failed: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    log.info("Completed {}", getName());
  }
}
