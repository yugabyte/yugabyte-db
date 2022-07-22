// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.BootstrapUniverseResponse;
import org.yb.client.YBClient;

@Slf4j
public class BootstrapProducer extends XClusterConfigTaskBase {

  public static final long MINIMUM_ADMIN_OPERATION_TIMEOUT_MS_FOR_BOOTSTRAP = 120000;

  @Inject
  protected BootstrapProducer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The source universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Table ids to bootstrap.
    public List<String> tableIds;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s (sourceUniverse=%s, xClusterUuid=%s, tableIds=%s)",
        super.getName(),
        taskParams().universeUUID,
        taskParams().xClusterConfig.uuid,
        taskParams().tableIds);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // Each bootstrap producer task must belong to a parent xCluster config.
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    Universe sourceUniverse = Universe.getOrBadRequest(taskParams().universeUUID);
    String sourceUniverseMasterAddresses = sourceUniverse.getMasterAddresses();
    String sourceUniverseCertificate = sourceUniverse.getCertificateNodetoNode();
    // Bootstrapping producer might be slower compared to other operations, and it has to have a
    // minimum of 120 seconds timeout.
    YBClientService.Config clientConfig =
        new YBClientService.Config(
            sourceUniverseMasterAddresses,
            sourceUniverseCertificate,
            Math.max(
                YBClientService.Config.DEFAULT_ADMIN_OPERATION_TIMEOUT_MS,
                MINIMUM_ADMIN_OPERATION_TIMEOUT_MS_FOR_BOOTSTRAP));
    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
      // Set bootstrap creation time.
      Date now = new Date();
      xClusterConfig.setBootstrapCreateTimeForTables(taskParams().tableIds, now);
      log.info("Bootstrap creation time for tables {} set to {}", taskParams().tableIds, now);

      // Todo: Add retry to other tservers if the first tserver is failing.
      // Set the IP:Port of the first tserver in the list of tservers.
      HostAndPort hostAndPort =
          HostAndPort.fromParts(
              sourceUniverse.getTServersInPrimaryCluster().get(0).cloudInfo.private_ip,
              sourceUniverse.getTServersInPrimaryCluster().get(0).tserverRpcPort);
      // Do the bootstrap.
      BootstrapUniverseResponse resp = client.bootstrapUniverse(hostAndPort, taskParams().tableIds);
      if (resp.hasError()) {
        String errMsg =
            String.format(
                "Failed to bootstrap universe (%s) for table (%s): %s",
                taskParams().universeUUID, taskParams().tableIds, resp.errorMessage());
        throw new RuntimeException(errMsg);
      }
      List<String> bootstrapIds = resp.bootstrapIds();
      if (bootstrapIds.size() != taskParams().tableIds.size()) {
        String errMsg =
            String.format(
                "Received invalid number of bootstrap ids (%d), must be (%d)",
                bootstrapIds.size(), taskParams().tableIds.size());
        throw new RuntimeException(errMsg);
      }

      // Save bootstrap ids.
      for (int i = 0; i < taskParams().tableIds.size(); i++) {
        Optional<XClusterTableConfig> tableConfig =
            xClusterConfig.maybeGetTableById(taskParams().tableIds.get(i));
        String bootstrapId = bootstrapIds.get(i);
        if (tableConfig.isPresent()) {
          tableConfig.get().streamId = bootstrapId;
          log.info("Stream id for table {} set to {}", tableConfig.get().tableId, bootstrapId);
        } else {
          // This code will never run because when we set the bootstrap creation time, we made sure
          // that all the tableIds exist.
          String errMsg =
              String.format(
                  "Could not find tableId (%s) in the xCluster config with uuid (%s)",
                  taskParams().tableIds.get(i), taskParams().xClusterConfig.uuid);
          throw new RuntimeException(errMsg);
        }
      }
      xClusterConfig.update();

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse(true).incrementVersion();
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
