// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.gflags.GFlagsValidation.AutoFlagsPerServer;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Queue;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.cdc.CdcConsumer;
import org.yb.cdc.CdcConsumer.StreamEntryPB;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.GetReplicationStatusResponse;
import org.yb.client.GetXClusterSafeTimeResponse;
import org.yb.client.IsBootstrapRequiredResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.NamespaceSafeTimePB;
import org.yb.master.MasterReplicationOuterClass.ReplicationStatusPB;

@Singleton
@Slf4j
public class XClusterUniverseService {

  private static final long INITILA_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_IS_BOOTSTRAP_REQUIRED =
      1000; // 1 second
  private static final long MAXIMUM_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_IS_BOOTSTRAP_REQUIRED =
      300000; // 5 minutes

  private static final long ADMIN_OPERATION_TIMEOUT_MS_FOR_IS_BOOTSTRAP_REQUIRED_FAST_RESPONSE =
      60000; // 1 minute

  private final GFlagsValidation gFlagsValidation;
  private final RuntimeConfGetter confGetter;
  private final YBClientService ybService;
  private final AutoFlagUtil autoFlagUtil;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final YbClientConfigFactory ybClientConfigFactory;
  private static final String IS_BOOTSTRAP_REQUIRED_POOL_NAME =
      "xcluster.is_bootstrap_required_rpc_pool";
  public final ThreadPoolExecutor isBootstrapRequiredExecutor;
  private static final int IS_BOOTSTRAP_REQUIRED_RPC_PARTITION_SIZE = 32;
  private static final int IS_BOOTSTRAP_REQUIRED_RPC_MAX_RETRIES_NUMBER = 4;

  @Inject
  public XClusterUniverseService(
      GFlagsValidation gFlagsValidation,
      RuntimeConfGetter confGetter,
      YBClientService ybService,
      AutoFlagUtil autoFlagUtil,
      PlatformExecutorFactory platformExecutorFactory,
      YbClientConfigFactory ybClientConfigFactory) {
    this.gFlagsValidation = gFlagsValidation;
    this.confGetter = confGetter;
    this.ybService = ybService;
    this.autoFlagUtil = autoFlagUtil;
    this.platformExecutorFactory = platformExecutorFactory;
    this.isBootstrapRequiredExecutor =
        platformExecutorFactory.createExecutor(
            IS_BOOTSTRAP_REQUIRED_POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("IsBootstrapRequiredRpc-%d").build());
    this.ybClientConfigFactory = ybClientConfigFactory;
  }

  public Set<UUID> getActiveXClusterSourceAndTargetUniverseSet(UUID universeUUID) {
    return getActiveXClusterSourceAndTargetUniverseSet(
        universeUUID, new HashSet<>() /* excludeXClusterConfigSet */);
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
   * Get the set of universes UUID which are connected to the input universe either as target
   * universe through a running xCluster config.
   *
   * @param universeUUID the universe on which search needs to be performed.
   * @return the set of universe uuid which are connected to the input universe.
   */
  public Set<UUID> getActiveXClusterTargetUniverseSet(UUID universeUUID) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByUniverseUuid(universeUUID).stream()
            .filter(
                xClusterConfig ->
                    !xClusterConfig
                        .getStatus()
                        .equals(XClusterConfig.XClusterConfigStatusType.DeletedUniverse))
            .collect(Collectors.toList());
    return xClusterConfigs.stream()
        .filter(config -> config.getSourceUniverseUUID().equals(universeUUID))
        .map(config -> config.getTargetUniverseUUID())
        .collect(Collectors.toSet());
  }

  public Set<Universe> getXClusterConnectedUniverses(Universe initialUniverse) {
    return getXClusterConnectedUniverses(
        initialUniverse, new HashSet<>() /* excludeXClusterConfigSet */);
  }

  /**
   * Returns the set of universes of xCluster connected universes.
   *
   * @param initialUniverse the initial point of the xCluster nexus.
   * @param excludeXClusterConfigSet set of universe which will be ignored.
   * @return universe set containing xCluster connected universes.
   */
  public Set<Universe> getXClusterConnectedUniverses(
      Universe initialUniverse, Set<UUID> excludeXClusterConfigSet) {
    Set<Universe> universeSet = new HashSet<>();
    Queue<Universe> universeQueue = new LinkedList<>();
    Set<UUID> visitedUniverse = new HashSet<>();
    universeQueue.add(initialUniverse);
    visitedUniverse.add(initialUniverse.getUniverseUUID());
    while (universeQueue.size() > 0) {
      Universe universe = universeQueue.remove();
      universeSet.add(universe);
      Set<UUID> xClusterUniverses =
          getActiveXClusterSourceAndTargetUniverseSet(
              universe.getUniverseUUID(), excludeXClusterConfigSet);
      if (!CollectionUtils.isEmpty(xClusterUniverses)) {
        for (UUID univUUID : xClusterUniverses) {
          if (!visitedUniverse.contains(univUUID)) {
            universeQueue.add(Universe.getOrBadRequest(univUUID));
            visitedUniverse.add(univUUID);
          }
        }
      }
    }
    return universeSet;
  }

  /**
   * Checks if we can perform promote auto flags on the provided universe set. All universes need to
   * be auto flags compatible and should be supporting same list of auto flags.
   *
   * @param universeSet
   * @param univUpgradeInProgress
   * @param upgradeUniverseSoftwareVersion
   * @return true if auto flags can be promoted on all universes.
   * @throws IOException
   * @throws PlatformServiceException
   */
  public boolean canPromoteAutoFlags(
      Set<Universe> universeSet,
      Universe univUpgradeInProgress,
      String upgradeUniverseSoftwareVersion)
      throws IOException, PlatformServiceException {
    AutoFlagsPerServer masterAutoFlags =
        gFlagsValidation.extractAutoFlags(upgradeUniverseSoftwareVersion, "yb-master");
    AutoFlagsPerServer tserverAutoFlags =
        gFlagsValidation.extractAutoFlags(upgradeUniverseSoftwareVersion, "yb-tserver");
    // Compare auto flags json for each universe.
    for (Universe univ : universeSet) {
      // Once rollback support is enabled, auto flags will be promoted through finalize api.
      if (!confGetter.getConfForScope(univ, UniverseConfKeys.promoteAutoFlag)) {
        return false;
      }
      if (univ.getUniverseUUID().equals(univUpgradeInProgress.getUniverseUUID())) {
        continue;
      }
      String softwareVersion =
          univ.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      if (!CommonUtils.isAutoFlagSupported(softwareVersion)) {
        return false;
      }
      AutoFlagsPerServer univMasterAutoFlags =
          gFlagsValidation.extractAutoFlags(softwareVersion, "yb-master");
      AutoFlagsPerServer univTServerAutoFlags =
          gFlagsValidation.extractAutoFlags(softwareVersion, "yb-tserver");
      if (!(compareAutoFlagPerServerListByName(
              masterAutoFlags.autoFlagDetails, univMasterAutoFlags.autoFlagDetails)
          && compareAutoFlagPerServerListByName(
              tserverAutoFlags.autoFlagDetails, univTServerAutoFlags.autoFlagDetails))) {
        return false;
      }
    }
    return true;
  }

  /**
   * Compare list of auto flags details on the basis of name irrespective of their order.
   *
   * @param x first auto flag details.
   * @param y second auto flag details.
   * @return true if both lists of auto flag details are same.
   */
  public boolean compareAutoFlagPerServerListByName(
      List<GFlagsValidation.AutoFlagDetails> x, List<GFlagsValidation.AutoFlagDetails> y) {
    List<String> xFlagsNameList = x.stream().map(flag -> flag.name).collect(Collectors.toList());
    List<String> yFlagsNameList = y.stream().map(flag -> flag.name).collect(Collectors.toList());
    return CommonUtils.isEqualIgnoringOrder(xFlagsNameList, yFlagsNameList);
  }

  /**
   * Fetches the multiple set of xCluster connected universe.
   *
   * @param universeSet
   * @return
   */
  public Set<Set<Universe>> getMultipleXClusterConnectedUniverseSet(
      Set<UUID> universeSet, Set<UUID> excludeXClusterConfigSet) {
    Set<Set<Universe>> multipleXClusterConnectedUniverseSet = new HashSet<>();
    Set<UUID> visitedUniverseSet = new HashSet<>();
    for (UUID universeUUID : universeSet) {
      if (visitedUniverseSet.contains(universeUUID)) {
        continue;
      }
      Universe universe = Universe.getOrBadRequest(universeUUID);
      Set<Universe> xClusterConnectedUniverses =
          getXClusterConnectedUniverses(universe, excludeXClusterConfigSet);
      multipleXClusterConnectedUniverseSet.add(xClusterConnectedUniverses);
      visitedUniverseSet.addAll(
          xClusterConnectedUniverses.stream()
              .map(Universe::getUniverseUUID)
              .collect(Collectors.toList()));
    }
    return multipleXClusterConnectedUniverseSet;
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
        "XClusterUniverseService.isBootstrapRequired is called with xClusterConfig={}, "
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
      log.debug(
          "Using existing stream id map with {} entries for {} tables " + "for isBootstrapRequired",
          tableIdStreamIdMap.size(),
          tableIds.size());
    } else {
      log.debug("No stream ids available for isBootstrapRequired");
      tableIdStreamIdMap = new HashMap<>();
      tableIds.forEach(tableId -> tableIdStreamIdMap.put(tableId, null));
    }

    Universe sourceUniverse = Universe.getOrBadRequest(sourceUniverseUuid);
    String sourceUniverseMasterAddresses = sourceUniverse.getMasterAddresses();
    // If there is no queryable master, return the empty map.
    if (sourceUniverseMasterAddresses.isEmpty()) {
      return isBootstrapRequiredMap;
    }
    String sourceUniverseCertificate = sourceUniverse.getCertificateNodetoNode();
    // When ignoreErrors is true the request comes from the UI, and it expects a fast response.
    long ybClientTimeout =
        ignoreErrors
            ? ADMIN_OPERATION_TIMEOUT_MS_FOR_IS_BOOTSTRAP_REQUIRED_FAST_RESPONSE
            : confGetter.getGlobalConf(GlobalConfKeys.ybcAdminOperationTimeoutMs);
    YbClientConfig clientConfig =
        ybClientConfigFactory.create(
            sourceUniverseMasterAddresses,
            sourceUniverseCertificate,
            ybClientTimeout,
            ybClientTimeout);
    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
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

        int maxPoolSize =
            confGetter.getGlobalConf(GlobalConfKeys.xclusterBootstrapRequiredRpcMaxThreads);
        if (maxPoolSize != this.isBootstrapRequiredExecutor.getMaximumPoolSize()) {
          this.isBootstrapRequiredExecutor.setMaximumPoolSize(maxPoolSize);
        }

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
                        // If ignoreErrors is true, a fast response is expected so do not retry.
                        if (ignoreErrors) {
                          log.debug(
                              "Not retrying isBootstrapRequired RPC because ignoreErrors is true");
                          break;
                        }
                        // Busy waiting is unavoidable.
                        Thread.sleep(
                            Util.getExponentialBackoffDelayMs(
                                INITILA_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_IS_BOOTSTRAP_REQUIRED,
                                MAXIMUM_EXPONENTIAL_BACKOFF_DELAY_MS_FOR_IS_BOOTSTRAP_REQUIRED,
                                iterationNumber));
                        iterationNumber++;
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

  public List<ReplicationStatusPB> getReplicationStatus(XClusterConfig xClusterConfig) {
    log.debug(
        "XClusterUniverseService.getReplicationStatus is called with xClusterConfig={}",
        xClusterConfig);

    Set<String> streamIds =
        xClusterConfig.getTableDetails().stream()
            .filter(XClusterTableConfig::isReplicationSetupDone)
            .map(XClusterTableConfig::getStreamId)
            .collect(Collectors.toSet());

    // If there is no table in replication, there is no corresponding replication group.
    if (streamIds.isEmpty()) {
      return Collections.emptyList();
    }

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YbClientConfig clientConfig =
        ybClientConfigFactory.create(targetUniverseMasterAddresses, targetUniverseCertificate);
    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
      GetReplicationStatusResponse resp =
          client.getReplicationStatus(xClusterConfig.getReplicationGroupName());
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "GetReplicationStatus RPC call with %s has errors in xCluster config %s: %s",
                xClusterConfig.getReplicationGroupName(), xClusterConfig, resp.errorMessage()));
      }
      List<ReplicationStatusPB> statuses = resp.getStatuses();
      log.debug(
          "GetReplicationStatus RPC call with {} returned {}",
          xClusterConfig.getReplicationGroupName(),
          statuses);

      Set<String> streamIdsWithStatus =
          statuses.stream()
              .map(replicationStatus -> replicationStatus.getStreamId().toStringUtf8())
              .collect(Collectors.toSet());
      Set<String> notFoundStreamIds = Sets.difference(streamIds, streamIdsWithStatus);
      Set<String> extraStreamIds = Sets.difference(streamIdsWithStatus, streamIds);
      if (!notFoundStreamIds.isEmpty() || !extraStreamIds.isEmpty()) {
        log.warn(
            "GetReplicationStatus RPC call does not have streamIds {} and includes extra "
                + "streamIds {}; please sync",
            notFoundStreamIds,
            extraStreamIds);
        if (confGetter.getConfForScope(
            targetUniverse, UniverseConfKeys.ensureSyncGetReplicationStatus)) {
          throw new RuntimeException(
              String.format(
                  "GetReplicationStatus RPC call does not have streamIds %s and includes "
                      + "extra streamIds %s; please sync",
                  notFoundStreamIds, extraStreamIds));
        }
      }

      return statuses;
    } catch (Exception e) {
      log.error("XClusterUniverseService.GetReplicationStatus hit error : {}", e.getMessage());
      throw new RuntimeException(e);
    }
  }

  public List<NamespaceSafeTimePB> getNamespaceSafeTimeList(XClusterConfig xClusterConfig) {
    log.debug(
        "XClusterUniverseService.getNamespaceSafeTimeList is called with xClusterConfig={}",
        xClusterConfig);

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YbClientConfig clientConfig =
        ybClientConfigFactory.create(targetUniverseMasterAddresses, targetUniverseCertificate);
    try (YBClient client = ybService.getClientWithConfig(clientConfig)) {
      GetXClusterSafeTimeResponse resp = client.getXClusterSafeTime();
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "getXClusterSafeTime RPC call has errors in xCluster config %s: %s",
                xClusterConfig, resp.errorMessage()));
      }
      return resp.getSafeTimes();
    } catch (Exception e) {
      log.error("XClusterUniverseService.getNamespaceSafeTimeList hit error : {}", e.getMessage());
      throw new RuntimeException(e);
    }
  }

  public Map<String, String> getSourceTableIdTargetTableIdMap(
      Universe targetUniverse, String replicationGroupName) {
    String universeMasterAddresses = targetUniverse.getMasterAddresses();
    String universeCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      GetMasterClusterConfigResponse clusterConfigResp = client.getMasterClusterConfig();
      if (clusterConfigResp.hasError()) {
        String errMsg =
            String.format(
                "Failed to getMasterClusterConfig from target universe (%s) for "
                    + "replicationGroupName "
                    + "(%s): %s",
                targetUniverse.getUniverseUUID(),
                replicationGroupName,
                clusterConfigResp.errorMessage());
        throw new RuntimeException(errMsg);
      }
      CatalogEntityInfo.SysClusterConfigEntryPB config = clusterConfigResp.getConfig();
      CdcConsumer.ProducerEntryPB replicationGroup =
          config.getConsumerRegistry().getProducerMapMap().get(replicationGroupName);
      if (replicationGroup == null) {
        throw new RuntimeException(
            String.format(
                "No replication group found with name (%s) in universe (%s) cluster config",
                replicationGroupName, targetUniverse.getUniverseUUID()));
      }

      Map<String, CdcConsumer.StreamEntryPB> replicationStreams =
          replicationGroup.getStreamMapMap();
      Map<String, String> sourceTableIdTargetTableIdMap =
          replicationStreams.values().stream()
              .collect(
                  Collectors.toMap(
                      StreamEntryPB::getProducerTableId, StreamEntryPB::getConsumerTableId));
      log.debug(
          "XClusterUniverseService.getSourceTableIdTargetTableIdMap: "
              + "sourceTableIdTargetTableIdMap is {}",
          sourceTableIdTargetTableIdMap);
      return sourceTableIdTargetTableIdMap;
    } catch (Exception e) {
      log.error(
          "XClusterUniverseService.getSourceTableIdTargetTableIdMap hit error : {}",
          e.getMessage());
      throw new RuntimeException(e);
    }
  }

  public Set<UUID> getXClusterTargetUniverseSetToBeImpactedWithUpgradeFinalize(Universe universe)
      throws IOException {
    Set<UUID> result = new HashSet<>();
    Set<UUID> targetUniverseUUIDSet =
        getActiveXClusterTargetUniverseSet(universe.getUniverseUUID());
    Set<Universe> targetUniverseSet =
        targetUniverseUUIDSet.stream().map(Universe::getOrBadRequest).collect(Collectors.toSet());
    String softwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    for (ServerType serverType : ImmutableSet.of(ServerType.MASTER, ServerType.TSERVER)) {
      Set<String> autoFlagsAfterPromotionOnSourceUniverse =
          gFlagsValidation.extractAutoFlags(softwareVersion, serverType).autoFlagDetails.stream()
              .filter(flag -> flag.flagClass >= AutoFlagUtil.EXTERNAL_AUTO_FLAG_CLASS)
              .map(flag -> flag.name)
              .collect(Collectors.toSet());
      for (Universe targetUniverse : targetUniverseSet) {
        String targetUniverseVersion =
            targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
        if (!CommonUtils.isAutoFlagSupported(targetUniverseVersion)) {
          result.add(targetUniverse.getUniverseUUID());
          continue;
        }
        Set<String> promotedAutoFlagsOnTargetUniverse =
            autoFlagUtil.getPromotedAutoFlags(
                targetUniverse, serverType, AutoFlagUtil.LOCAL_PERSISTED_AUTO_FLAG_CLASS);
        if (!CommonUtils.isEqualIgnoringOrder(
            autoFlagsAfterPromotionOnSourceUniverse, promotedAutoFlagsOnTargetUniverse)) {
          result.add(targetUniverse.getUniverseUUID());
        }
      }
    }
    return result;
  }
}
