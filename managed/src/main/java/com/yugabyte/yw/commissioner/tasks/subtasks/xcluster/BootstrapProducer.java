// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
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
  public static final long MINIMUM_SOCKET_READ_TIMEOUT_MS_FOR_BOOTSTRAP = 120000;

  private final RuntimeConfGetter confGetter;
  private final YbClientConfigFactory ybcClientConfigFactory;

  @Inject
  protected BootstrapProducer(
      BaseTaskDependencies baseTaskDependencies,
      RuntimeConfGetter confGetter,
      YbClientConfigFactory ybcClientConfigFactory) {
    super(baseTaskDependencies);
    this.confGetter = confGetter;
    this.ybcClientConfigFactory = ybcClientConfigFactory;
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
        taskParams().getUniverseUUID(),
        taskParams().getXClusterConfig().getUuid(),
        taskParams().tableIds);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    long startTime = System.nanoTime();

    // Each bootstrap producer task must belong to a parent xCluster config.
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    xClusterConfig.updateStatusForTables(
        taskParams().tableIds, XClusterTableConfig.Status.Bootstrapping);

    Universe sourceUniverse = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String sourceUniverseMasterAddresses = sourceUniverse.getMasterAddresses();
    String sourceUniverseCertificate = sourceUniverse.getCertificateNodetoNode();
    // Bootstrapping producer might be slower compared to other operations, and it has to have a
    // minimum of 120 seconds timeout.
    YbClientConfig clientConfig =
        ybcClientConfigFactory.create(
            sourceUniverseMasterAddresses,
            sourceUniverseCertificate,
            Math.max(
                confGetter.getGlobalConf(GlobalConfKeys.ybcAdminOperationTimeoutMs),
                MINIMUM_ADMIN_OPERATION_TIMEOUT_MS_FOR_BOOTSTRAP),
            Math.max(
                confGetter.getGlobalConf(GlobalConfKeys.ybcSocketReadTimeoutMs),
                MINIMUM_SOCKET_READ_TIMEOUT_MS_FOR_BOOTSTRAP));
    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
      // Set bootstrap creation time.
      Date now = new Date();
      xClusterConfig.updateBootstrapCreateTimeForTables(taskParams().tableIds, now);
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
                taskParams().getUniverseUUID(), taskParams().tableIds, resp.errorMessage());
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
          tableConfig.get().setStreamId(bootstrapId);
          // If the table is bootstrapped, no need to bootstrap again.
          tableConfig.get().setNeedBootstrap(false);
          log.info("Stream id for table {} set to {}", tableConfig.get().getTableId(), bootstrapId);
        } else {
          // This code will never run because when we set the bootstrap creation time, we made sure
          // that all the tableIds exist.
          String errMsg =
              String.format(
                  "Could not find tableId (%s) in the xCluster config with uuid (%s)",
                  taskParams().tableIds.get(i), taskParams().getXClusterConfig().getUuid());
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

    log.info("Completed (time: {}) {}", System.nanoTime() - startTime, getName());
  }
}
