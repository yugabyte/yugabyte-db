package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.Util.getUUIDRepresentation;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import io.ebean.Ebean;
import java.time.Duration;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@Singleton
@Slf4j
public class PitrConfigPoller {

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final YBClientService ybClientService;

  private static final String YB_SNAPSHOT_SCHEDULED_RUN_INTERVAL =
      "yb.snapshot_schedule.run_interval";

  @Inject
  public PitrConfigPoller(
      PlatformScheduler platformScheduler,
      RuntimeConfigFactory runtimeConfigFactory,
      YBClientService ybClientService) {
    this.platformScheduler = platformScheduler;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.ybClientService = ybClientService;
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
    Map<UUID, Set<UUID>> scheduleMap =
        pitrConfigList
            .stream()
            .collect(
                Collectors.groupingBy(
                    p -> p.getUniverse().getUniverseUUID(),
                    Collectors.mapping(PitrConfig::getUuid, Collectors.toSet())));
    YBClient client = null;

    for (Map.Entry<UUID, Set<UUID>> entry : scheduleMap.entrySet()) {
      UUID universeUUID = entry.getKey();
      Universe universe = Universe.getOrBadRequest(universeUUID);
      String masterHostPorts = universe.getMasterAddresses();
      String certificate = universe.getCertificateNodetoNode();
      ListSnapshotSchedulesResponse scheduleResp;
      List<SnapshotScheduleInfo> scheduleInfoList;
      Set<UUID> snapshotScheduleUUIDs = entry.getValue();
      log.info("Universe uuid: {}, schedule uuid: {}", universeUUID, snapshotScheduleUUIDs);
      try {
        client = ybClientService.getClient(masterHostPorts, certificate);
        scheduleResp = client.listSnapshotSchedules(null);
        scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
      } catch (Exception ex) {
        log.error("Failed to get snapshots for universe {}", universeUUID, ex);
        continue;
      } finally {
        ybClientService.closeClient(client, masterHostPorts);
      }

      for (SnapshotScheduleInfo snapshotScheduleInfo : scheduleInfoList) {
        if (!snapshotScheduleUUIDs.contains(snapshotScheduleInfo.getSnapshotScheduleUUID())) {
          continue;
        }

        for (SnapshotInfo snapshotInfo : snapshotScheduleInfo.getSnapshotInfoList()) {
          if (snapshotInfo.getState().equals(State.FAILED)) {
            // TOOD: PLAT-4919 -> Create an alert for the snapshot failure
            log.error(
                "Failed state for snapshot: {} for universe: {}",
                snapshotInfo.getSnapshotUUID(),
                universeUUID);
          }
        }
      }
    }
  }
}
