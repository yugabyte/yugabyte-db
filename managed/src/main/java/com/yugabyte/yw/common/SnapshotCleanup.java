// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.Equator;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;

@Singleton
public class SnapshotCleanup {
  public static final Logger LOG = LoggerFactory.getLogger(SnapshotCleanup.class);

  private final YBClientService ybService;
  private final RuntimeConfGetter confGetter;

  @Inject
  public SnapshotCleanup(YBClientService ybService, RuntimeConfGetter confGetter) {
    this.ybService = ybService;
    this.confGetter = confGetter;
  }

  public List<SnapshotInfo> getNonScheduledSnapshotList(Universe universe) {
    List<SnapshotInfo> snapshotInfosList = new ArrayList<>();
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    if (!universe.getUniverseDetails().universePaused) {
      YBClient ybClient = null;
      try {
        ybClient = ybService.getClient(masterHostPorts, certificate);
        ListSnapshotSchedulesResponse snapshotScheduleResponse =
            ybClient.listSnapshotSchedules(null);
        List<SnapshotScheduleInfo> snapshotScheduleInfosList = new ArrayList<>();
        if (snapshotScheduleResponse != null
            && CollectionUtils.isNotEmpty(snapshotScheduleResponse.getSnapshotScheduleInfoList())) {
          snapshotScheduleInfosList = snapshotScheduleResponse.getSnapshotScheduleInfoList();
        }
        List<SnapshotInfo> scheduledSnapshotsInfos = new ArrayList<>();
        if (CollectionUtils.isNotEmpty(snapshotScheduleInfosList)) {
          scheduledSnapshotsInfos =
              snapshotScheduleInfosList
                  .stream()
                  .filter(sSI -> CollectionUtils.isNotEmpty(sSI.getSnapshotInfoList()))
                  .flatMap(sSI -> sSI.getSnapshotInfoList().stream())
                  .collect(Collectors.toList());
        }
        Optional.ofNullable(ybClient.listSnapshots(null, false).getSnapshotInfoList())
            .ifPresent(snapshotInfosList::addAll);
        return (List<SnapshotInfo>)
            CollectionUtils.removeAll(
                snapshotInfosList,
                scheduledSnapshotsInfos,
                new Equator<SnapshotInfo>() {

                  @Override
                  public boolean equate(SnapshotInfo o1, SnapshotInfo o2) {
                    return o1.getSnapshotUUID().equals(o2.getSnapshotUUID());
                  }

                  @Override
                  public int hash(SnapshotInfo o) {
                    return 0;
                  }
                });
      } catch (Exception e) {
        LOG.error("Error fetching Orphan snapshots for Universe: {}", universe.getUniverseUUID());
      } finally {
        ybService.closeClient(ybClient, masterHostPorts);
      }
    }
    return snapshotInfosList;
  }

  private List<SnapshotInfo> getFilterInProgressBackupSnapshots(
      List<SnapshotInfo> snapshotInfoList, Long upperBound) {
    return snapshotInfoList
        .stream()
        .filter(sI -> upperBound > 0 ? (sI.getSnapshotTime() <= upperBound) : true)
        .collect(Collectors.toList());
  }

  public void start() {
    Thread snapshotCleanerThread =
        new Thread(
            () -> {
              try {
                LOG.info("Started Orphan snapshot cleanup task");
                deleteOrphanSnapshots();
                LOG.info("Orphan snapshot cleanup task complete");
              } catch (Exception e) {
                LOG.warn("Orphan snapshot cleanup task failed", e);
              }
            });
    snapshotCleanerThread.start();
  }

  public void deleteOrphanSnapshots() {
    Customer.getAll()
        .forEach(
            c -> {
              c.getUniverseUUIDs()
                  .forEach(
                      (u) -> {
                        Optional<Universe> uOpt = Universe.maybeGet(u);
                        if (!(uOpt.isPresent()
                            && confGetter.getConfForScope(
                                uOpt.get(), UniverseConfKeys.deleteOrphanSnapshotOnStartup))) {
                          return;
                        }
                        Universe universe = uOpt.get();
                        ImmutablePair<UUID, Long> universeSnapshotFilterTime =
                            Backup.getUniverseInProgressBackupCreateTime(c.getUuid(), u);
                        List<SnapshotInfo> snapshotInfoList = getNonScheduledSnapshotList(universe);
                        snapshotInfoList =
                            getFilterInProgressBackupSnapshots(
                                snapshotInfoList, universeSnapshotFilterTime.right);
                        if (CollectionUtils.isNotEmpty(snapshotInfoList)) {
                          String masterHostPorts = universe.getMasterAddresses();
                          String certificate = universe.getCertificateNodetoNode();
                          YBClient ybClient = ybService.getClient(masterHostPorts, certificate);
                          try {
                            snapshotInfoList
                                .stream()
                                .forEach(
                                    sI -> {
                                      try {
                                        ybClient.deleteSnapshot(sI.getSnapshotUUID());
                                      } catch (Exception e) {
                                        LOG.error(
                                            "Unable to delete snapshot: {}", sI.getSnapshotUUID());
                                      }
                                    });
                          } catch (Exception e) {
                            LOG.error("Got error while deleting snapshots during Platform start.");
                          } finally {
                            ybService.closeClient(ybClient, masterHostPorts);
                          }
                        }
                      });
            });
  }
}
