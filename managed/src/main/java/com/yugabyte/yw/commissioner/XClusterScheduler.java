// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.Duration;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;

@Singleton
@Slf4j
public class XClusterScheduler {

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfGetter confGetter;
  private final YBClientService ybClientService;
  private final XClusterUniverseService xClusterUniverseService;
  private final MetricService metricService;
  private final UniverseTableHandler tableHandler;

  // This is a key lock for xCluster Config by UUID.
  private static final KeyLock<UUID> XCLUSTER_CONFIG_LOCK = new KeyLock<>();

  @Inject
  public XClusterScheduler(
      PlatformScheduler platformScheduler,
      RuntimeConfGetter confGetter,
      YBClientService ybClientService,
      XClusterUniverseService xClusterUniverseService,
      MetricService metricService,
      UniverseTableHandler tableHandler) {
    this.platformScheduler = platformScheduler;
    this.confGetter = confGetter;
    this.ybClientService = ybClientService;
    this.xClusterUniverseService = xClusterUniverseService;
    this.metricService = metricService;
    this.tableHandler = tableHandler;
  }

  private Duration getSyncSchedulerInterval() {
    return confGetter.getGlobalConf(GlobalConfKeys.xClusterSyncSchedulerInterval);
  }

  private Duration getMetricsSchedulerInterval() {
    return confGetter.getGlobalConf(GlobalConfKeys.xClusterMetricsSchedulerInterval);
  }

  public void start() {
    platformScheduler.schedule(
        "XCluster-Sync-Scheduler",
        Duration.ZERO /* initialDelay */,
        this.getSyncSchedulerInterval(),
        this::syncScheduleRunner);

    platformScheduler.schedule(
        "XCluster-Metrics-Scheduler",
        Duration.ZERO /* initialDelay */,
        this.getMetricsSchedulerInterval(),
        this::metricsScheduleRunner);
  }

  // Sync XCluster config methods.
  // --------------------------------------------------------------------------------

  private Set<String> filterIndexTables(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> allTableInfoList,
      Collection<String> filterTableIds) {
    return allTableInfoList.stream()
        .filter(tableInfo -> filterTableIds.contains(XClusterConfigTaskBase.getTableId(tableInfo)))
        .filter(TableInfoUtil::isIndexTable)
        .map(XClusterConfigTaskBase::getTableId)
        .collect(Collectors.toSet());
  }

  private boolean isXClusterEligibleForScheduledSync(XClusterConfig xClusterConfig) {
    if (xClusterConfig.getStatus() != XClusterConfigStatusType.Running) {
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
    return confGetter.getConfForScope(targetUniverse, UniverseConfKeys.xClusterSyncOnUniverse)
        && confGetter.getConfForScope(sourceUniverse, UniverseConfKeys.xClusterSyncOnUniverse);
  }

  private Set<String> getTableIdsToAdd(
      Set<String> tableIdsInReplication,
      Set<String> tableIdsInYbaXClusterConfig,
      Set<String> sourceUniverseTableIds) {
    return tableIdsInReplication.stream()
        .filter(
            tableId ->
                !tableIdsInYbaXClusterConfig.contains(tableId)
                    && sourceUniverseTableIds.contains(tableId))
        .collect(Collectors.toSet());
  }

  private Set<String> getTableIdsToRemove(
      Set<String> tableIdsInReplication,
      Set<String> tableIdsInYbaXClusterConfig,
      Set<String> sourceUniverseTableIds,
      Universe sourceUniverse) {

    Set<String> cdcStreamsInUniverse =
        xClusterUniverseService.getAllCDCStreamIdsInUniverse(ybClientService, sourceUniverse);

    return tableIdsInYbaXClusterConfig.stream()
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
              if (xClusterTableConfig.isEmpty()
                  || xClusterTableConfig.get().getStreamId() == null) {
                return false;
              }
              // Exclude tables that have a streamId present in the source universe's CDC
              // streams.
              return !cdcStreamsInUniverse.contains(xClusterTableConfig.get().getStreamId());
            })
        .collect(Collectors.toSet());
  }

  private void updateXClusterConfig(
      XClusterConfig config, CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig) {
    updateXClusterConfig(
        config,
        clusterConfig,
        Collections.emptyList(),
        Collections.emptySet(),
        Collections.emptySet());
  }

  private void updateXClusterConfig(
      XClusterConfig config,
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceUniverseTableInfoList,
      Set<String> tableIdsToAdd,
      Set<String> tableIdsToRemove) {

    // remove tables
    if (!CollectionUtils.isEmpty(tableIdsToRemove)) {
      log.info("Tables to remove {}", tableIdsToRemove);
      config.removeTables(tableIdsToRemove);
    }

    // add tables
    if (!CollectionUtils.isEmpty(tableIdsToAdd)) {
      Set<String> indexTableIdsToAdd =
          filterIndexTables(sourceUniverseTableInfoList, tableIdsToAdd);
      Set<String> nonIndexTableIdsToAdd =
          tableIdsToAdd.stream()
              .filter(tableId -> !indexTableIdsToAdd.contains(tableId))
              .collect(Collectors.toSet());

      if (!CollectionUtils.isEmpty(indexTableIdsToAdd)) {
        log.info("Index tables to add {}", indexTableIdsToAdd);
        config.addTablesIfNotExist(indexTableIdsToAdd, null, true);
      }
      if (!CollectionUtils.isEmpty(nonIndexTableIdsToAdd)) {
        log.info("Non index Tables to add {}", nonIndexTableIdsToAdd);
        config.addTablesIfNotExist(nonIndexTableIdsToAdd, null, false);
      }
    }

    XClusterConfigTaskBase.syncXClusterConfigWithReplicationGroup(
        clusterConfig, config, tableIdsToAdd);
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
      log.error("Error getting cluster config for xCluster config:", e);
      return;
    }

    if (config.getType() != XClusterConfig.ConfigType.Db) {
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceUniverseTableInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybClientService, sourceUniverse);

      Set<String> sourceUniverseTableIds =
          sourceUniverseTableInfoList.stream()
              .map(XClusterConfigTaskBase::getTableId)
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
    } else {
      updateXClusterConfig(config, clusterConfig);
    }
  }

  public void syncXClusterConfig(XClusterConfig config) {
    try {
      XCLUSTER_CONFIG_LOCK.acquireLock(config.getUuid());
      if (!isXClusterEligibleForScheduledSync(config)) {
        log.debug("Skipping xCluster config {} for scheduled sync", config.getName());
        return;
      }
      compareTablesAndSyncXClusterConfig(config);
    } catch (Exception e) {
      log.error("Error syncing xCluster config:", e);
    } finally {
      try {
        XCLUSTER_CONFIG_LOCK.releaseLock(config.getUuid());
      } catch (Exception e) {
        log.error("Error releasing lock for xCluster config:", e);
      }
    }
  }

  private void syncScheduleRunner() {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping scheduler for follower platform");
      return;
    }
    log.info("Running xCluster Sync Scheduler...");
    try {
      List<XClusterConfig> xClusterConfigs = XClusterConfig.getAllXClusterConfigs();
      xClusterConfigs.forEach(this::syncXClusterConfig);
    } catch (Exception e) {
      log.error("Error running xCluster Sync Scheduler:", e);
    }
  }

  // --------------------------------------------------------------------------------
  // End of Sync XCluster config methods.

  // Publish XCluster config metrics methods.
  // --------------------------------------------------------------------------------

  private Metric buildMetricTemplate(
      XClusterConfig xClusterConfig, XClusterTableConfig xClusterTableConfig) {
    XClusterTableConfig.Status tableStatus = xClusterTableConfig.getStatus();
    if (xClusterTableConfig.getStatus().equals(XClusterTableConfig.Status.Running)) {
      if (xClusterTableConfig.getReplicationStatusErrors().size() > 0) {
        tableStatus = XClusterTableConfig.Status.ReplicationError;
      }
    }

    double value = tableStatus.getCode();

    return MetricService.buildMetricTemplate(PlatformMetrics.XCLUSTER_TABLE_STATUS)
        .setExpireTime(
            CommonUtils.nowPlusWithoutMillis(
                MetricService.DEFAULT_METRIC_EXPIRY_SEC, ChronoUnit.SECONDS))
        .setKeyLabel(KnownAlertLabels.TABLE_UUID, xClusterTableConfig.getTableId())
        .setLabel(
            KnownAlertLabels.SOURCE_UNIVERSE_UUID,
            xClusterConfig.getSourceUniverseUUID().toString())
        .setLabel(
            KnownAlertLabels.TARGET_UNIVERSE_UUID,
            xClusterConfig.getTargetUniverseUUID().toString())
        .setLabel(KnownAlertLabels.TABLE_TYPE, xClusterConfig.getTableType().toString())
        .setLabel(KnownAlertLabels.XCLUSTER_CONFIG_UUID, xClusterConfig.getUuid().toString())
        .setLabel(KnownAlertLabels.XCLUSTER_CONFIG_NAME, xClusterConfig.getName())
        .setLabel(
            KnownAlertLabels.XCLUSTER_REPLICATION_GROUP_NAME,
            xClusterConfig.getReplicationGroupName())
        .setValue(value);
  }

  private List<Metric> collectMetrics(XClusterConfig xClusterConfig) {
    List<Metric> metricsList = new ArrayList<>();
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    if (!xClusterConfig.getStatus().equals(XClusterConfigStatusType.Running)) {
      return metricsList;
    }
    if (sourceUniverse.getUniverseDetails().updateInProgress
        || targetUniverse.getUniverseDetails().updateInProgress) {
      return metricsList;
    }
    if (sourceUniverse.getUniverseDetails().universePaused
        || targetUniverse.getUniverseDetails().universePaused) {
      return metricsList;
    }

    XClusterConfigTaskBase.updateReplicationDetailsFromDB(
        xClusterUniverseService, ybClientService, tableHandler, xClusterConfig);
    Set<XClusterTableConfig> xClusterTableConfigs = xClusterConfig.getTableDetails();
    xClusterTableConfigs.forEach(
        tableConfig -> {
          metricsList.add(buildMetricTemplate(xClusterConfig, tableConfig));
        });

    return metricsList;
  }

  private void metricsScheduleRunner() {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping scheduler for follower platform");
      return;
    }
    log.info("Running xCluster Metrics Scheduler...");
    try {
      List<XClusterConfig> xClusterConfigs = XClusterConfig.getAllXClusterConfigs();
      List<Metric> metricsList = new ArrayList<>();
      for (XClusterConfig config : xClusterConfigs) {
        metricsList.addAll(collectMetrics(config));
      }
      MetricFilter toClean =
          MetricFilter.builder().metricName(PlatformMetrics.XCLUSTER_TABLE_STATUS).build();
      metricService.cleanAndSave(metricsList, toClean);
      metricService.setOkStatusMetric(
          MetricService.buildMetricTemplate(PlatformMetrics.XCLUSTER_METRIC_PROCESSOR_STATUS));
    } catch (Exception e) {
      metricService.setFailureStatusMetric(
          MetricService.buildMetricTemplate(PlatformMetrics.XCLUSTER_METRIC_PROCESSOR_STATUS));
      log.error("Error running xCluster Metrics Scheduler: {}", e);
    }
  }
}
