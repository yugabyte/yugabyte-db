// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import io.jsonwebtoken.lang.Collections;
import java.time.Duration;
import java.util.Collection;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;

@Singleton
@Slf4j
public class XClusterSyncScheduler {

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfGetter confGetter;
  private final YBClientService ybClientService;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  public XClusterSyncScheduler(
      PlatformScheduler platformScheduler,
      RuntimeConfGetter confGetter,
      YBClientService ybClientService,
      XClusterUniverseService xClusterUniverseService) {
    this.platformScheduler = platformScheduler;
    this.confGetter = confGetter;
    this.ybClientService = ybClientService;
    this.xClusterUniverseService = xClusterUniverseService;
  }

  private Duration getSchedulerInterval() {
    return confGetter.getGlobalConf(GlobalConfKeys.xClusterSyncSchedulerInterval);
  }

  public void start() {
    platformScheduler.schedule(
        "XClusterSyncScheduler",
        Duration.ZERO /* initialDelay */,
        this.getSchedulerInterval(),
        this::scheduleRunner);
  }

  private Set<String> filterIndexTables(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> allTableInfoList,
      Collection<String> filterTableIds) {
    return allTableInfoList.stream()
        .filter(tableInfo -> filterTableIds.contains(XClusterConfigTaskBase.getTableId(tableInfo)))
        .filter(tableInfo -> TableInfoUtil.isIndexTable(tableInfo))
        .map(tableInfo -> XClusterConfigTaskBase.getTableId(tableInfo))
        .collect(Collectors.toSet());
  }

  public boolean isXClusterEligibleForScheduledSync(XClusterConfig xClusterConfig) {
    if (!xClusterConfig.getStatus().equals(XClusterConfigStatusType.Running)) {
      return false;
    }

    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    if (sourceUniverse.getUniverseDetails().updateInProgress
        || targetUniverse.getUniverseDetails().updateInProgress) {
      return false;
    }
    if (sourceUniverse.getUniverseDetails().universePaused
        || targetUniverse.getUniverseDetails().universePaused) {
      return false;
    }
    if (!confGetter.getConfForScope(targetUniverse, UniverseConfKeys.xClusterSyncOnUniverse)
        || !confGetter.getConfForScope(sourceUniverse, UniverseConfKeys.xClusterSyncOnUniverse)) {
      return false;
    }
    return true;
  }

  private Set<String> getTableIdsToAdd(
      Set<String> tableIdsInReplication,
      Set<String> tableIdsInYbaXClusterConfig,
      Set<String> sourceUniverseTableIds) {
    Set<String> tableIdsToAdd =
        tableIdsInReplication.stream()
            .filter(
                tableId ->
                    !tableIdsInYbaXClusterConfig.contains(tableId)
                        && sourceUniverseTableIds.contains(tableId))
            .collect(Collectors.toSet());
    return tableIdsToAdd;
  }

  private Set<String> getTableIdsToRemove(
      Set<String> tableIdsInReplication,
      Set<String> tableIdsInYbaXClusterConfig,
      Set<String> sourceUniverseTableIds,
      Universe sourceUniverse) {

    Set<String> cdcStreamsInUniverse =
        xClusterUniverseService.getAllCDCStreamsInUniverse(ybClientService, sourceUniverse);

    Set<String> tableIdsToRemove =
        tableIdsInYbaXClusterConfig.stream()
            .filter(
                tableId -> {
                  // Exclude tables that are still present in the replication group.
                  if (tableIdsInReplication.contains(tableId)) {
                    return false;
                  }
                  // Exclude tables that are still present in the source universe.
                  if (sourceUniverseTableIds.contains(tableId)) {
                    return false;
                  }
                  // Exclude tables that have no associated xClusterTableConfig or have a null
                  // streamId.
                  Optional<XClusterTableConfig> xClusterTableConfig =
                      XClusterTableConfig.maybeGetByTableId(tableId);
                  if (!xClusterTableConfig.isPresent()
                      || xClusterTableConfig.get().getStreamId() == null) {
                    return false;
                  }
                  // Exclude tables that have a streamId present in the source universe's CDC
                  // streams.
                  if (cdcStreamsInUniverse.contains(xClusterTableConfig.get().getStreamId())) {
                    return false;
                  }
                  return true;
                })
            .collect(Collectors.toSet());

    return tableIdsToRemove;
  }

  private void updateXClusterConfig(
      XClusterConfig config,
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceUniverseTableInfoList,
      Set<String> tableIdsToAdd,
      Set<String> tableIdsToRemove) {

    // remove tables
    if (!Collections.isEmpty(tableIdsToRemove)) {
      log.info("Tables to remove {}", tableIdsToRemove);
      config.removeTables(tableIdsToRemove);
    }

    // add tables
    if (!Collections.isEmpty(tableIdsToAdd)) {
      Set<String> indexTableIdsToAdd =
          filterIndexTables(sourceUniverseTableInfoList, tableIdsToAdd);
      Set<String> nonIndexTableIdsToAdd =
          tableIdsToAdd.stream()
              .filter(tableId -> !indexTableIdsToAdd.contains(tableId))
              .collect(Collectors.toSet());

      if (!Collections.isEmpty(indexTableIdsToAdd)) {
        log.info("Index tables to add {}", indexTableIdsToAdd);
        config.addTablesIfNotExist(indexTableIdsToAdd, null, true);
      }
      if (!Collections.isEmpty(nonIndexTableIdsToAdd)) {
        log.info("Non index Tables to add {}", nonIndexTableIdsToAdd);
        config.addTablesIfNotExist(nonIndexTableIdsToAdd, null, false);
      }
      XClusterConfigTaskBase.syncXClusterConfigWithReplicationGroup(
          clusterConfig, config, tableIdsToAdd);
    }
  }

  public void compareTablesAndSyncXClusterConfig(XClusterConfig config) {
    Universe targetUniverse = Universe.getOrBadRequest(config.getTargetUniverseUUID());
    Universe sourceUniverse = Universe.getOrBadRequest(config.getSourceUniverseUUID());

    // Get the cluster configuration for the target universe
    CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig;
    try (YBClient client =
        ybClientService.getClient(
            targetUniverse.getMasterAddresses(), targetUniverse.getCertificateNodetoNode())) {
      clusterConfig =
          XClusterConfigTaskBase.getClusterConfig(client, config.getTargetUniverseUUID());
    } catch (Exception e) {
      log.error("Error getting cluster config for xCluster config: {}", e);
      return;
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceUniverseTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybClientService, sourceUniverse);

    Set<String> sourceUniverseTableIds =
        sourceUniverseTableInfoList.stream()
            .map(tableInfo -> XClusterConfigTaskBase.getTableId(tableInfo))
            .collect(Collectors.toSet());

    Set<String> tableIdsInReplication =
        XClusterConfigTaskBase.getProducerTableIdsFromClusterConfig(
            clusterConfig, config.getReplicationGroupName());

    Set<String> tableIdsInYbaXClusterConfig = config.getTableIds();

    Set<String> tableIdsToAdd =
        getTableIdsToAdd(
            tableIdsInReplication, tableIdsInYbaXClusterConfig, sourceUniverseTableIds);

    Set<String> tableIdsToRemove =
        getTableIdsToRemove(
            tableIdsInReplication,
            tableIdsInYbaXClusterConfig,
            sourceUniverseTableIds,
            sourceUniverse);

    if (CollectionUtils.isEmpty(tableIdsToRemove) && CollectionUtils.isEmpty(tableIdsToAdd)) {
      log.debug("No tables to add or remove for xCluster config {}", config.getName());
      return;
    }

    updateXClusterConfig(
        config, clusterConfig, sourceUniverseTableInfoList, tableIdsToAdd, tableIdsToRemove);
  }

  public synchronized void syncXClusterConfig(XClusterConfig config) {
    try {
      if (!isXClusterEligibleForScheduledSync(config)) {
        log.debug("Skipping xCluster config {} for scheduled sync", config.getName());
        return;
      }
      compareTablesAndSyncXClusterConfig(config);
    } catch (Exception e) {
      log.error("Error syncing xCluster config: {}", e);
    } finally {
    }
  }

  private void scheduleRunner() {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping scheduler for follower platform");
      return;
    }
    log.info("Running xCluster Sync Scheduler...");
    try {
      List<XClusterConfig> xClusterConfigs = XClusterConfig.getAllXClusterConfigs();
      xClusterConfigs.forEach(
          config -> {
            syncXClusterConfig(config);
          });
    } catch (Exception e) {
      log.error("Error running xCluster Sync Scheduler: {}", e);
    }
  }
}
