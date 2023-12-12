// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import java.time.Duration;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.RollbackAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterTypes.MasterErrorPB;

@Slf4j
public class RollbackAutoFlags extends ServerSubTaskBase {

  private static final long SLEEP_AFTER_ROLLBACK_MS = 1000 * 180;

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

    try (YBClient client = getClient()) {
      RollbackAutoFlagsResponse resp = client.rollbackAutoFlags(taskParams().rollbackVersion);
      if (resp.hasError()) {
        MasterErrorPB error = resp.getError();
        log.error(error.toString());
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, error.toString());
      }
      if (resp.getFlagsRolledBack()) {
        log.info(
            "Some AutoFlags were rolled back. Sleeping {} ms to allow in-flight operations to"
                + " complete",
            SLEEP_AFTER_ROLLBACK_MS);
        waitFor(Duration.ofMillis(SLEEP_AFTER_ROLLBACK_MS));
      }
    } catch (Exception e) {
      log.error("Rollback AutoFlag task failed: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    log.info("Completed {}", getName());
  }
}
