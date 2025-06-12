package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.metrics.MetricService.STATUS_NOT_OK;
import static com.yugabyte.yw.common.metrics.MetricService.STATUS_OK;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;

@Singleton
@Slf4j
public class PitrConfigPoller {

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final YBClientService ybClientService;
  private final MetricService metricService;

  private static final String YB_SNAPSHOT_SCHEDULED_RUN_INTERVAL =
      "yb.snapshot_schedule.run_interval";

  @Inject
  public PitrConfigPoller(
      PlatformScheduler platformScheduler,
      RuntimeConfigFactory runtimeConfigFactory,
      YBClientService ybClientService,
      MetricService metricService) {
    this.platformScheduler = platformScheduler;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.ybClientService = ybClientService;
    this.metricService = metricService;
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, this.scheduleInterval(), this::scheduleRunner);
  }

  private Duration scheduleInterval() {
    return runtimeConfigFactory
        .staticApplicationConf()
        .getDuration(YB_SNAPSHOT_SCHEDULED_RUN_INTERVAL);
  }

  void scheduleRunner() {
    log.info("Running PITR Config Poller");
    List<PitrConfig> pitrConfigList = PitrConfig.getAll();
    Map<UUID, Map<UUID, PitrConfig>> scheduleMap =
        pitrConfigList.stream()
            .collect(
                Collectors.groupingBy(
                    p -> p.getUniverse().getUniverseUUID(),
                    Collectors.toMap(PitrConfig::getUuid, Function.identity())));

    List<Metric> metrics = new ArrayList<>();
    for (Map.Entry<UUID, Map<UUID, PitrConfig>> entry : scheduleMap.entrySet()) {
      UUID universeUUID = entry.getKey();
      try {
        Universe universe = Universe.getOrBadRequest(universeUUID);
        if (universe.getUniverseDetails().universePaused) {
          continue;
        }
        ListSnapshotSchedulesResponse scheduleResp;
        List<SnapshotScheduleInfo> scheduleInfoList;
        Map<UUID, PitrConfig> snapshotScheduleMap = entry.getValue();
        Set<UUID> snapshotScheduleUUIDs = snapshotScheduleMap.keySet();
        log.info("Universe uuid: {}, schedule uuid: {}", universeUUID, snapshotScheduleUUIDs);
        try (YBClient client = ybClientService.getUniverseClient(universe)) {
          scheduleResp = client.listSnapshotSchedules(null);
          scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
        } catch (Exception ex) {
          log.error("Failed to get snapshots for universe {}", universeUUID, ex);
          continue;
        }

        for (SnapshotScheduleInfo snapshotScheduleInfo : scheduleInfoList) {
          if (!snapshotScheduleUUIDs.contains(snapshotScheduleInfo.getSnapshotScheduleUUID())) {
            continue;
          }
          PitrConfig pitrConfig =
              snapshotScheduleMap.get(snapshotScheduleInfo.getSnapshotScheduleUUID());
          boolean pitrStatus =
              BackupUtil.allSnapshotsSuccessful(snapshotScheduleInfo.getSnapshotInfoList());

          if (pitrStatus) {
            metrics.add(
                BackupUtil.buildMetricTemplate(
                    PlatformMetrics.PITR_CONFIG_STATUS, universe, pitrConfig, STATUS_OK));
          } else {
            log.error(
                "Failed state for PITR config: {} for universe: {}",
                pitrConfig.getUuid(),
                universeUUID);
            metrics.add(
                BackupUtil.buildMetricTemplate(
                    PlatformMetrics.PITR_CONFIG_STATUS, universe, pitrConfig, STATUS_NOT_OK));
          }
        }
      } catch (Exception ex) {
        log.error(
            "Not able to update the latest snapshot schedule status for the universe: "
                + universeUUID.toString());
      }
    }
    MetricFilter toClean =
        MetricFilter.builder()
            .metricNames(Collections.singletonList(PlatformMetrics.PITR_CONFIG_STATUS))
            .build();
    metricService.cleanAndSave(metrics, toClean);
  }
}
