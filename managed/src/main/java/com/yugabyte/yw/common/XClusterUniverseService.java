// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.IsBootstrapRequiredResponse;
import org.yb.client.YBClient;

@Singleton
@Slf4j
public class XClusterUniverseService {

  private static final long INITILA_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_IS_BOOTSTRAP_REQUIRED =
      1000; // 1 second
  private static final long MAXIMUM_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_IS_BOOTSTRAP_REQUIRED =
      300000; // 5 minutes

  private final GFlagsValidation gFlagsValidation;
  private final RuntimeConfGetter confGetter;
  private final YBClientService ybService;
  private final PlatformExecutorFactory platformExecutorFactory;
  private static final String IS_BOOTSTRAP_REQUIRED_POOL_NAME =
      "xcluster.is_bootstrap_required_rpc_pool";
  private final ExecutorService isBootstrapRequiredExecutor;
  private static final int IS_BOOTSTRAP_REQUIRED_RPC_PARTITION_SIZE = 8;
  private static final int IS_BOOTSTRAP_REQUIRED_RPC_MAX_RETRIES_NUMBER = 4;

  @Inject
  public XClusterUniverseService(
      GFlagsValidation gFlagsValidation,
      RuntimeConfGetter confGetter,
      YBClientService ybService,
      PlatformExecutorFactory platformExecutorFactory) {
    this.gFlagsValidation = gFlagsValidation;
    this.confGetter = confGetter;
    this.ybService = ybService;
    this.platformExecutorFactory = platformExecutorFactory;
    this.isBootstrapRequiredExecutor =
        platformExecutorFactory.createExecutor(
            IS_BOOTSTRAP_REQUIRED_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("IsBootstrapRequiredRpc-%d").build());
  }

  /**
   * Get the set of universes UUID which are connected to the input universe either as source or
   * target universe through a running xCluster config.
   *
   * @param universeUUID the universe on which search needs to be performed.
   * @param excludeXClusterConfigSet set of universe which will be ignored.
   * @return the set of universe uuid which are connected to the input universe.
   */
  public Set<UUID> getActiveXClusterSourceAndTargetUniverseSet(
      UUID universeUUID, Set<UUID> excludeXClusterConfigSet) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByUniverseUuid(universeUUID).stream()
            .filter(
                xClusterConfig ->
                    !xClusterConfig
                        .getStatus()
                        .equals(XClusterConfig.XClusterConfigStatusType.DeletedUniverse))
            .filter(xClusterConfig -> !excludeXClusterConfigSet.contains(xClusterConfig.getUuid()))
            .collect(Collectors.toList());
    return xClusterConfigs.stream()
        .map(
            config -> {
              if (config.getSourceUniverseUUID().equals(universeUUID)) {
                return config.getTargetUniverseUUID();
              } else {
                return config.getSourceUniverseUUID();
              }
            })
        .collect(Collectors.toSet());
  }

  /**
   * It creates the required parameters to make IsBootstrapRequired API call and then makes the
   * call.
   *
   * @param tableIds The table IDs of tables to check whether they need bootstrap
   * @param xClusterConfig The config to check if an existing stream has fallen far behind
   * @param sourceUniverseUuid The UUID of the universe that {@code tableIds} belong to
   * @param ignoreErrors Whether it could ignore errors and return partial results
   * @return A map of tableId to a boolean showing whether that table needs bootstrapping
   */
  public Map<String, Boolean> isBootstrapRequired(
      Set<String> tableIds,
      @Nullable XClusterConfig xClusterConfig,
      UUID sourceUniverseUuid,
      boolean ignoreErrors)
      throws Exception {
    log.debug(
        "XClusterConfigTaskBase.isBootstrapRequired is called with xClusterConfig={}, "
            + "tableIds={}, and universeUuid={}",
        xClusterConfig,
        tableIds,
        sourceUniverseUuid);
    Map<String, Boolean> isBootstrapRequiredMap = new HashMap<>();

    // If there is no table to check, return the empty map.
    if (tableIds.isEmpty()) {
      return isBootstrapRequiredMap;
    }

    // Create tableIdStreamId map to pass to the IsBootstrapRequired API.
    Map<String, String> tableIdStreamIdMap;
    if (xClusterConfig != null) {
      tableIdStreamIdMap = xClusterConfig.getTableIdStreamIdMap(tableIds);
    } else {
      tableIdStreamIdMap = new HashMap<>();
      tableIds.forEach(tableId -> tableIdStreamIdMap.put(tableId, null));
    }

    Universe sourceUniverse = Universe.getOrBadRequest(sourceUniverseUuid);
    String sourceUniverseMasterAddresses =
        sourceUniverse.getMasterAddresses(true /* mastersQueryable */);
    // If there is no queryable master, return the empty map.
    if (sourceUniverseMasterAddresses.isEmpty()) {
      return isBootstrapRequiredMap;
    }
    String sourceUniverseCertificate = sourceUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(sourceUniverseMasterAddresses, sourceUniverseCertificate)) {
      try {
        int partitionSize =
            XClusterConfigTaskBase.supportsMultipleTablesWithIsBootstrapRequired(sourceUniverse)
                ? IS_BOOTSTRAP_REQUIRED_RPC_PARTITION_SIZE
                : 1;
        log.info("Partition size used for isBootstrapRequiredParallel is {}", partitionSize);

        // Partition the tableIdStreamIdMap.
        List<Map<String, String>> tableIdStreamIdMapPartitions = new ArrayList<>();
        Iterator<Entry<String, String>> iter = tableIdStreamIdMap.entrySet().iterator();
        while (iter.hasNext()) {
          Map<String, String> partition = new HashMap<>();
          tableIdStreamIdMapPartitions.add(partition);

          while (partition.size() < partitionSize && iter.hasNext()) {
            Entry<String, String> entry = iter.next();
            partition.put(entry.getKey(), entry.getValue());
          }
        }
        log.debug("Partitioned the tableIds to {}", tableIdStreamIdMapPartitions);

        // Make the requests for all the partitions in parallel.
        List<Future<Map<String, Boolean>>> fs = new ArrayList<>();
        for (Map<String, String> tableIdStreamIdPartition : tableIdStreamIdMapPartitions) {
          fs.add(
              this.isBootstrapRequiredExecutor.submit(
                  () -> {
                    int iterationNumber = 0;
                    IsBootstrapRequiredResponse resp = null;
                    // Retry in case of error. It is specifically useful where a tablet leader
                    // election is in progress on the DB side.
                    while (iterationNumber < IS_BOOTSTRAP_REQUIRED_RPC_MAX_RETRIES_NUMBER
                        && Objects.isNull(resp)) {
                      try {
                        log.debug(
                            "Running IsBootstrapRequired RPC for tableIdStreamIdPartition {}",
                            tableIdStreamIdPartition);
                        resp = client.isBootstrapRequired(tableIdStreamIdPartition);
                        if (resp.hasError()) {
                          throw new RuntimeException(
                              String.format(
                                  "IsBootstrapRequired RPC call with %s has errors in "
                                      + "xCluster config %s: %s",
                                  xClusterConfig, tableIdStreamIdPartition, resp.errorMessage()));
                        }
                      } catch (Exception e) {
                        if (Objects.nonNull(e.getMessage())
                            && e.getMessage()
                                .contains("invalid method name: IsBootstrapRequired")) {
                          // It means the current YBDB version of the source universe does not
                          // support the IsBootstrapRequired RPC call. Ignore the error.
                          log.warn(
                              "XClusterConfigTaskBase.isBootstrapRequired hit error because "
                                  + "its corresponding RPC call does not exist in the source "
                                  + "universe {} (error is ignored) : {}",
                              sourceUniverse.getUniverseUUID(),
                              e.getMessage());
                          return null;
                        } else {
                          // Print the error and retry.
                          log.error(
                              "client.isBootstrapRequired RPC hit error : {}", e.getMessage());
                        }
                        resp = null;
                        iterationNumber++;
                        // If ignoreErrors is true, a fast response is expected.
                        if (!ignoreErrors) {
                          // Busy waiting is unavoidable.
                          Thread.sleep(
                              Util.getExponentialBackoffDelayMs(
                                  INITILA_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_IS_BOOTSTRAP_REQUIRED,
                                  MAXIMUM_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_IS_BOOTSTRAP_REQUIRED,
                                  iterationNumber));
                        }
                      }
                    }
                    return Objects.nonNull(resp) ? resp.getResults() : null;
                  }));
        }

        // Gather all the futures' results.
        for (Future<Map<String, Boolean>> f : fs) {
          Optional.ofNullable(f.get()).ifPresent(isBootstrapRequiredMap::putAll);
        }

        Set<String> tableIdsRPCFailed =
            tableIdStreamIdMap.keySet().stream()
                .filter(tableId -> !isBootstrapRequiredMap.containsKey(tableId))
                .collect(Collectors.toSet());
        if (!tableIdsRPCFailed.isEmpty()) {
          log.warn("IsBootstrapRequired RPC call has failed for {}", tableIdsRPCFailed);
          if (!ignoreErrors) {
            throw new RuntimeException(
                String.format("IsBootstrapRequired RPC call has failed for %s", tableIdsRPCFailed));
          }
        }

        log.debug(
            "IsBootstrapRequired RPC call with {} returned {}",
            tableIdStreamIdMap,
            isBootstrapRequiredMap);

        return isBootstrapRequiredMap;
      } catch (Exception e) {
        log.error("XClusterUniverseService.isBootstrapRequired hit error : {}", e.getMessage());
        throw new RuntimeException(e);
      }
    }
  }

  public Map<String, Boolean> isBootstrapRequired(
      Set<String> tableIds, @Nullable XClusterConfig xClusterConfig, UUID sourceUniverseUuid)
      throws Exception {
    return isBootstrapRequired(
        tableIds, xClusterConfig, sourceUniverseUuid, false /* ignoreErrors */);
  }
}
