// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
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
import java.util.Objects;
import java.util.Optional;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.BootstrapUniverseResponse;
import org.yb.client.YBClient;

@Slf4j
public class BootstrapProducer extends XClusterConfigTaskBase {

  public static final long MINIMUM_ADMIN_OPERATION_TIMEOUT_MS_FOR_BOOTSTRAP = 120000;
  public static final long MINIMUM_SOCKET_READ_TIMEOUT_MS_FOR_BOOTSTRAP = 120000;

  private static final long INITIAL_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_BOOTSTRAP_PRODUCER =
      1000; // 1 second
  private static final long MAXIMUM_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_BOOTSTRAP_PRODUCER =
      60000; // 1 minute

  private final RuntimeConfGetter confGetter;
  private final YbClientConfigFactory ybcClientConfigFactory;

  @Inject
  protected BootstrapProducer(
      BaseTaskDependencies baseTaskDependencies,
      RuntimeConfGetter confGetter,
      YbClientConfigFactory ybcClientConfigFactory,
      XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
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
        "%s (sourceUniverse=%s, xClusterUuid=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().getXClusterConfig().getUuid());
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
                confGetter.getGlobalConf(GlobalConfKeys.bootstrapProducerTimeoutMs),
                MINIMUM_ADMIN_OPERATION_TIMEOUT_MS_FOR_BOOTSTRAP),
            Math.max(
                confGetter.getGlobalConf(GlobalConfKeys.ybcSocketReadTimeoutMs),
                MINIMUM_SOCKET_READ_TIMEOUT_MS_FOR_BOOTSTRAP));
    List<HostAndPort> tserverHostAndPortList =
        sourceUniverse.getTServersInPrimaryCluster().stream()
            .map(
                tserverNodeDetails ->
                    HostAndPort.fromParts(
                        tserverNodeDetails.cloudInfo.private_ip, tserverNodeDetails.tserverRpcPort))
            .collect(Collectors.toList());

    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
      // Set bootstrap creation time.
      Date now = new Date();
      xClusterConfig.updateBootstrapCreateTimeForTables(taskParams().tableIds, now);
      log.info("Bootstrap creation time for tables {} set to {}", taskParams().tableIds, now);

      int tserverIndex = 0;
      BootstrapUniverseResponse resp = null;
      while (tserverIndex < tserverHostAndPortList.size() && Objects.isNull(resp)) {
        try {
          // Do the bootstrap.
          HostAndPort hostAndPort = tserverHostAndPortList.get(tserverIndex);
          resp = client.bootstrapUniverse(hostAndPort, taskParams().tableIds);
          if (resp.hasError()) {
            String errMsg =
                String.format(
                    "Failed to bootstrap universe (%s) for table (%s): %s",
                    taskParams().getUniverseUUID(), taskParams().tableIds, resp.errorMessage());
            throw new RuntimeException(errMsg);
          }
        } catch (Exception e) {
          // Print the error and retry.
          log.error("client.bootstrapUniverse RPC hit error : {}", e.getMessage());
          resp = null;
          // Busy waiting is unavoidable.
          Thread.sleep(
              Util.getExponentialBackoffDelayMs(
                  INITIAL_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_BOOTSTRAP_PRODUCER,
                  MAXIMUM_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_BOOTSTRAP_PRODUCER,
                  tserverIndex /* iterationNumber */));
        } finally {
          tserverIndex++;
        }
      }

      if (Objects.isNull(resp)) {
        throw new RuntimeException(
            String.format("BootstrapProducer RPC call has failed for %s", taskParams().tableIds));
      }

      List<String> bootstrapIds = resp.bootstrapIds();
      if (bootstrapIds.size() != taskParams().tableIds.size()) {
        String errMsg =
            String.format(
                "Received invalid number of bootstrap ids (%d), must be (%d)",
                bootstrapIds.size(), taskParams().tableIds.size());
        throw new IllegalStateException(errMsg);
      }

      // Save bootstrap ids.
      for (int i = 0; i < taskParams().tableIds.size(); i++) {
        Optional<XClusterTableConfig> tableConfig =
            xClusterConfig.maybeGetTableById(taskParams().tableIds.get(i));
        int idx = i;
        tableConfig.ifPresentOrElse(
            config -> {
              String bootstrapId = bootstrapIds.get(idx);
              config.setStreamId(bootstrapId);
              // If the table is bootstrapped, no need to bootstrap again.
              config.setNeedBootstrap(false);
              log.info("Stream id for table {} set to {}", config.getTableId(), bootstrapId);
            },
            () -> {
              // This code will never run because when we set the bootstrap creation time, we made
              // sure
              // that all the tableIds exist.
              String errMsg =
                  String.format(
                      "Could not find tableId (%s) in the xCluster config with uuid (%s)",
                      taskParams().tableIds.get(idx), taskParams().getXClusterConfig().getUuid());
              throw new RuntimeException(errMsg);
            });
      }

      xClusterConfig.update();

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse().incrementVersion();
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed (time: {}) {}", System.nanoTime() - startTime, getName());
  }
}
