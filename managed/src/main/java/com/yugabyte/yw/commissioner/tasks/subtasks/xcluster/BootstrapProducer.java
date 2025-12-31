// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.BootstrapUniverseResponse;
import org.yb.client.DeleteCDCStreamResponse;
import org.yb.client.ListCDCStreamsResponse;
import org.yb.client.YBClient;

@Slf4j
public class BootstrapProducer extends XClusterConfigTaskBase {
  private static final long INITIAL_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_BOOTSTRAP_PRODUCER =
      1000; // 1 second
  private static final long MAXIMUM_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_BOOTSTRAP_PRODUCER =
      60000; // 1 minute

  private final YbClientConfigFactory ybcClientConfigFactory;

  @Inject
  protected BootstrapProducer(
      BaseTaskDependencies baseTaskDependencies,
      RuntimeConfGetter confGetter,
      YbClientConfigFactory ybcClientConfigFactory,
      XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
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
        "%s (sourceUniverse=%s,xClusterUuid=%s,tableIds=%s)",
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
    try (YBClient client = getClientToBootstrapProducer(sourceUniverse)) {
      //      Map<String, String> tableIdToBootstrapIdMap = new HashMap<>();
      Map<String, List<String>> tableIdToBootstrapIdsMap = new HashMap<>();
      try {
        ListCDCStreamsResponse listCDCStreamsResp =
            client.listCDCStreams(null /* tableId */, null /* namespace */, null /* idType */);
        if (listCDCStreamsResp.hasError()) {
          String errMsg =
              String.format(
                  "Failed to listCDCStreams universe (%s): %s",
                  taskParams().getUniverseUUID(), listCDCStreamsResp.errorMessage());
          throw new RuntimeException(errMsg);
        }
        listCDCStreamsResp
            .getStreams()
            .forEach(
                stream -> {
                  if (isStreamInfoForXCluster(stream)
                      && Objects.equals(stream.getOptions().get("state"), "INITIATED")) {
                    List<String> streamTableIds = stream.getTableIds();
                    if (streamTableIds.size() != 1) {
                      return;
                    }
                    String tableId = streamTableIds.get(0);
                    if (taskParams().tableIds.contains(tableId)) {
                      if (!tableIdToBootstrapIdsMap.containsKey(tableId)) {
                        tableIdToBootstrapIdsMap.put(tableId, new ArrayList<>());
                      }
                      tableIdToBootstrapIdsMap.get(tableId).add(stream.getStreamId());
                    }
                  }
                });
      } catch (Exception e) {
        log.error("client.listCDCStreams RPC hit error : {}", e.getMessage());
        throw new RuntimeException(e);
      }
      log.trace("tableIdToBootstrapIdsMap from listCDCStreams is {}", tableIdToBootstrapIdsMap);

      // For switchover task, we want to clean up all the streams and start from scratch. For create
      // task, we can reuse the streams in INITIATED status.
      List<String> tableIdsWithoutBootstrapId;
      Set<String> bootstrapIdsToDelete = new HashSet<>();
      if (taskParams().updatingTask == TaskType.SwitchoverDrConfig) {
        // Delete the streams and recreated them (PLAT-18913).
        if (!tableIdToBootstrapIdsMap.isEmpty()) {
          log.info(
              "Tables {} are already bootstrapped; cleaning them up",
              tableIdToBootstrapIdsMap.keySet());
          xClusterConfig.updateBootstrapCreateTimeForTables(
              tableIdToBootstrapIdsMap.keySet(), null /* moment */);
          bootstrapIdsToDelete =
              tableIdToBootstrapIdsMap.values().stream()
                  .flatMap(List::stream)
                  .collect(Collectors.toSet());
        }
        tableIdsWithoutBootstrapId = taskParams().tableIds;
      } else {
        // Reuse the stream id stored in yba db and clean up the rest.
        Map<String, String> tableIdToStreamIdStoredInYBA =
            xClusterConfig.getTableIdStreamIdMap(
                xClusterConfig.getTableIds().stream()
                    .filter(tableId -> taskParams().tableIds.contains(tableId))
                    .collect(Collectors.toSet()));

        Set<String> tableIdsWithBootstrapIdAlreadySetup = new HashSet<>();
        for (Map.Entry<String, List<String>> entry : tableIdToBootstrapIdsMap.entrySet()) {
          String tableId = entry.getKey();
          List<String> bootstrapIds = entry.getValue();
          bootstrapIdsToDelete.addAll(
              bootstrapIds.stream()
                  .filter(
                      bootstrapId -> !bootstrapId.equals(tableIdToStreamIdStoredInYBA.get(tableId)))
                  .collect(Collectors.toSet()));
          if (bootstrapIds.contains(tableIdToStreamIdStoredInYBA.get(tableId))) {
            tableIdsWithBootstrapIdAlreadySetup.add(tableId);
          }
        }
        if (!tableIdsWithBootstrapIdAlreadySetup.isEmpty()) {
          log.info("Tables {} are already bootstrapped", tableIdsWithBootstrapIdAlreadySetup);
        }
        tableIdsWithoutBootstrapId =
            taskParams().tableIds.stream()
                .filter(tableId -> !tableIdsWithBootstrapIdAlreadySetup.contains(tableId))
                .toList();
      }

      if (!bootstrapIdsToDelete.isEmpty()) {
        log.trace("Deleting stream ids: {}", bootstrapIdsToDelete);
        // Need force delete = true for dangling streams.
        DeleteCDCStreamResponse deleteCDCStreamResponse =
            client.deleteCDCStream(
                bootstrapIdsToDelete, false /* ignoreErrors */, true /* forceDelete */);
        if (deleteCDCStreamResponse.hasError()) {
          throw new RuntimeException(
              String.format(
                  "Error deleting cdc streams: %s for universe: %s. Error: %s",
                  bootstrapIdsToDelete,
                  sourceUniverse.getName(),
                  deleteCDCStreamResponse.errorMessage()));
        }
      }

      if (!tableIdsWithoutBootstrapId.isEmpty()) {
        Map<String, String> newTableIdToBootstrapIdMap =
            bootstrapProducer(xClusterConfig, sourceUniverse, client, tableIdsWithoutBootstrapId);

        // Save bootstrap ids.
        newTableIdToBootstrapIdMap.forEach(
            (tableId, bootstrapId) -> {
              Optional<XClusterTableConfig> tableConfig = xClusterConfig.maybeGetTableById(tableId);
              tableConfig.ifPresentOrElse(
                  config -> {
                    config.setStreamId(bootstrapId);
                    // If the table is bootstrapped, no need to bootstrap again.
                    config.setNeedBootstrap(false);
                    log.info("Stream id for table {} set to {}", tableId, bootstrapId);
                  },
                  () -> {
                    // This code will never run because when we set the bootstrap creation time, we
                    // made sure that all the tableIds exist.
                    String errMsg =
                        String.format(
                            "Could not find tableId (%s) in the xCluster config with uuid (%s)",
                            tableId, taskParams().getXClusterConfig().getUuid());
                    throw new IllegalStateException(errMsg);
                  });
            });
        xClusterConfig.update();

        if (HighAvailabilityConfig.get().isPresent()) {
          getUniverse().incrementVersion();
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed (time: {}) {}", System.nanoTime() - startTime, getName());
  }

  protected YBClient getClientToBootstrapProducer(Universe universe) {
    String sourceUniverseMasterAddresses = universe.getMasterAddresses();
    String sourceUniverseCertificate = universe.getCertificateNodetoNode();
    // Bootstrapping producer might be slower compared to other operations, and it has to have a
    // minimum of 120 seconds timeout.
    YbClientConfig clientConfig =
        ybcClientConfigFactory.create(
            sourceUniverseMasterAddresses,
            sourceUniverseCertificate,
            confGetter
                .getConfForScope(universe, UniverseConfKeys.xclusterBootstrapProducerTimeout)
                .toMillis(),
            confGetter
                .getConfForScope(universe, UniverseConfKeys.xclusterBootstrapProducerTimeout)
                .toMillis());
    return ybService.getClientWithConfig(clientConfig);
  }

  protected Map<String, String> bootstrapProducer(
      XClusterConfig xClusterConfig,
      Universe sourceUniverse,
      YBClient client,
      List<String> tableIdsWithoutBootstrapId) {
    // Set bootstrap creation time.
    Date now = new Date();
    xClusterConfig.updateBootstrapCreateTimeForTables(tableIdsWithoutBootstrapId, now);
    log.info("Bootstrap creation time for tables {} set to {}", tableIdsWithoutBootstrapId, now);

    List<HostAndPort> tserverHostAndPortList =
        sourceUniverse.getTServersInPrimaryCluster().stream()
            .map(
                tserverNodeDetails ->
                    HostAndPort.fromParts(
                        tserverNodeDetails.cloudInfo.private_ip, tserverNodeDetails.tserverRpcPort))
            .collect(Collectors.toList());
    int tserverIndex = 0;
    BootstrapUniverseResponse resp = null;
    while (tserverIndex < tserverHostAndPortList.size() && Objects.isNull(resp)) {
      try {
        // Do the bootstrap.
        HostAndPort hostAndPort = tserverHostAndPortList.get(tserverIndex);
        resp = client.bootstrapUniverse(hostAndPort, tableIdsWithoutBootstrapId);
        if (resp.hasError()) {
          String errMsg =
              String.format(
                  "Failed to bootstrap universe (%s) for table (%s): %s",
                  taskParams().getUniverseUUID(), tableIdsWithoutBootstrapId, resp.errorMessage());
          throw new RuntimeException(errMsg);
        }
      } catch (Exception e) {
        // Print the error and retry.
        log.error("client.bootstrapUniverse RPC hit error : {}", e.getMessage());
        resp = null;
        waitFor(
            Duration.ofMillis(
                Util.getExponentialBackoffDelayMs(
                    INITIAL_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_BOOTSTRAP_PRODUCER,
                    MAXIMUM_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_BOOTSTRAP_PRODUCER,
                    tserverIndex /* iterationNumber */)));
      } finally {
        tserverIndex++;
      }
    }
    if (Objects.isNull(resp)) {
      throw new RuntimeException(
          String.format(
              "BootstrapProducer RPC call has failed for %s", tableIdsWithoutBootstrapId));
    }
    List<String> bootstrapIds = resp.bootstrapIds();
    if (bootstrapIds.size() != tableIdsWithoutBootstrapId.size()) {
      String errMsg =
          String.format(
              "Received invalid number of bootstrap ids (%d), must be (%d)",
              bootstrapIds.size(), tableIdsWithoutBootstrapId.size());
      throw new IllegalStateException(errMsg);
    }
    Map<String, String> tableIdToBootstrapIdMap = new HashMap<>();
    for (int i = 0; i < tableIdsWithoutBootstrapId.size(); i++) {
      tableIdToBootstrapIdMap.put(tableIdsWithoutBootstrapId.get(i), bootstrapIds.get(i));
    }
    return tableIdToBootstrapIdMap;
  }
}
