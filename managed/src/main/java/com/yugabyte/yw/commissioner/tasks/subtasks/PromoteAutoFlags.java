// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterTypes.MasterErrorPB;

@Slf4j
public class PromoteAutoFlags extends ServerSubTaskBase {

  private final String MAX_AUTO_FLAG_CLASS = "kExternal";
  private final boolean PROMOTE_NON_RUNTIME_FLAG = true;

  @Inject
  protected PromoteAutoFlags(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    try (YBClient client = getClient()) {
      PromoteAutoFlagsResponse resp =
          client.promoteAutoFlags(
              MAX_AUTO_FLAG_CLASS,
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
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Promote AutoFlag task failed: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }
}
