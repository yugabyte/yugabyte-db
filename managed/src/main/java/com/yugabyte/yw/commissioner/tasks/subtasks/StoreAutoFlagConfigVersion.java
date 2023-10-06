// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;
import org.yb.WireProtocol;
import org.yb.client.YBClient;

@Slf4j
public class StoreAutoFlagConfigVersion extends UniverseTaskBase {

  @Inject
  protected StoreAutoFlagConfigVersion(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {}

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
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;

    int autoFlagConfigVersion;
    try {
      client = ybService.getClient(hostPorts, certificate);
      WireProtocol.AutoFlagsConfigPB autoFlagsConfigPB =
          client.autoFlagsConfig().getAutoFlagsConfig();
      autoFlagConfigVersion = autoFlagsConfigPB.getConfigVersion();
    } catch (Exception e) {
      log.error("Error while fetching auto flag config version ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybService.closeClient(client, hostPorts);
    }

    try {
      // Create the update lambda.
      Universe.UniverseUpdater updater =
          new Universe.UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              // If this universe is not being updated, fail the request.
              if (!universeDetails.updateInProgress) {
                String msg =
                    "UserUniverse " + taskParams().getUniverseUUID() + " is not being updated.";
                log.error(msg);
                throw new RuntimeException(msg);
              }

              UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
                  new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
              ybSoftwareConfig.setSoftwareVersion(
                  universeDetails.getPrimaryCluster().userIntent.ybSoftwareVersion);
              ybSoftwareConfig.setAutoFlagConfigVersion(autoFlagConfigVersion);
              universeDetails.prevYBSoftwareConfig = ybSoftwareConfig;

              universe.setUniverseDetails(universeDetails);
            }
          };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }

    log.info("Completed {}", getName());
  }
}
