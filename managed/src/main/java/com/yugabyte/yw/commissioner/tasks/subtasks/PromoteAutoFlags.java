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
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterTypes.MasterErrorPB;

@Slf4j
public class PromoteAutoFlags extends ServerSubTaskBase {

  private final boolean PROMOTE_NON_RUNTIME_FLAG = true;

  @Inject
  protected PromoteAutoFlags(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {
    // Whether to ignore errors during subtask execution. It will be used when the parent task is
    // forced.
    public boolean ignoreErrors;
    public String maxClass;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s (universeUuid=%s, ignoreErrors=%s)",
        super.getName(), taskParams().getUniverseUUID(), taskParams().ignoreErrors);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    try (YBClient client = getClient()) {
      PromoteAutoFlagsResponse resp =
          client.promoteAutoFlags(
              taskParams().maxClass,
              PROMOTE_NON_RUNTIME_FLAG,
              confGetter.getConfForScope(universe, UniverseConfKeys.promoteAutoFlagsForceFully));
      if (resp.hasError()) {
        MasterErrorPB error = resp.getError();
        // Ignore error with code 6 as there was no new flags to promote.
        if (error.getStatus().getCode().getNumber() == 6) {
          log.warn(error.toString());
        } else {
          log.error(error.toString());
          throw new PlatformServiceException(INTERNAL_SERVER_ERROR, error.toString());
        }
      }
      Duration sleepTime =
          confGetter.getConfForScope(
              universe, UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds);
      log.info("waiting for {} ms after updating auto flags", sleepTime.toMillis());
      if (sleepTime.toMillis() > 0) {
        waitFor(sleepTime);
      }
    } catch (PlatformServiceException pe) {
      log.error("Promote auto flags task failed: ", pe);
      if (!taskParams().ignoreErrors) {
        throw pe;
      }
      log.warn(
          "Promote auto flags task failed, but the error is ignored because "
              + "taskParams().ignoreErrors is true");
    } catch (Exception e) {
      log.error("Promote AutoFlag task failed: ", e);
      if (!taskParams().ignoreErrors) {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
      }
      log.warn(
          "Promote auto flags task failed, but the error is ignored because "
              + "taskParams().ignoreErrors is true");
    }
    log.info("Completed {}", getName());
  }
}
