// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import org.yb.master.MasterTypes.MasterErrorPB;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.YBClient;

@Slf4j
public class PromoteAutoFlags extends ServerSubTaskBase {

  private final String MAX_AUTO_FLAG_CLASS = "kExternal";
  private final boolean PROMOTE_NON_RUNTIME_FLAG = true;
  private final boolean PROMOTE_AUTO_FLAG_FORCEFULLY = false;

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
      // Ping each servers to ensure their availability.
      universe
          .getNodes()
          .forEach(
              (node) -> {
                String address = node.cloudInfo.private_ip;
                boolean reachable = false;
                try {
                  reachable =
                      client.ping(address, node.masterRpcPort)
                          || client.ping(address, node.tserverRpcPort);
                } catch (Exception e) {
                  throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
                }
                if (!reachable) {
                  throw new PlatformServiceException(
                      INTERNAL_SERVER_ERROR,
                      address + " is not responding on either master or tserver");
                }
              });

      PromoteAutoFlagsResponse resp =
          client.promoteAutoFlags(
              MAX_AUTO_FLAG_CLASS, PROMOTE_NON_RUNTIME_FLAG, PROMOTE_AUTO_FLAG_FORCEFULLY);
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
