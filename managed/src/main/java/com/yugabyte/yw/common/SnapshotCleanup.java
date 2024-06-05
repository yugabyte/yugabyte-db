// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.Equator;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;
import play.libs.Json;

@Singleton
public class SnapshotCleanup {
  public static final Logger LOG = LoggerFactory.getLogger(SnapshotCleanup.class);
  public static final Set<State> RESTORE_STATES =
      Sets.immutableEnumSet(State.RESTORED, State.RESTORING, State.DELETING);

  private final YBClientService ybService;
  private final RuntimeConfGetter confGetter;

  @Inject
  public SnapshotCleanup(YBClientService ybService, RuntimeConfGetter confGetter) {
    this.ybService = ybService;
    this.confGetter = confGetter;
  }

  public List<SnapshotInfo> getNonScheduledSnapshotList(Universe universe) {
    List<SnapshotInfo> listWithoutRestoreSnapshots = new ArrayList<>();
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
              snapshotScheduleInfosList.stream()
                  .filter(sSI -> CollectionUtils.isNotEmpty(sSI.getSnapshotInfoList()))
                  .flatMap(sSI -> sSI.getSnapshotInfoList().stream())
                  .collect(Collectors.toList());
        }
        List<SnapshotInfo> snapshotInfosList = new ArrayList<>();
        Optional.ofNullable(ybClient.listSnapshots(null, false).getSnapshotInfoList())
            .ifPresent(snapshotInfosList::addAll);
        listWithoutRestoreSnapshots =
            snapshotInfosList.stream()
                .filter(sI -> !RESTORE_STATES.contains(sI.getState()))
                .collect(Collectors.toList());
        return (List<SnapshotInfo>)
            CollectionUtils.removeAll(
                listWithoutRestoreSnapshots,
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
    return listWithoutRestoreSnapshots;
  }

  private List<SnapshotInfo> getFilterInProgressBackupSnapshots(
      List<SnapshotInfo> snapshotInfoList, Long upperBound) {
    return snapshotInfoList.stream()
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
    Map<UUID, BackupRequestParams> backupParamsMap = new HashMap<>();
    List<TaskInfo> tInfoList = TaskInfo.getLatestIncompleteBackupTask();
    if (CollectionUtils.isNotEmpty(tInfoList)) {
      tInfoList.stream()
          .map(tInfo -> Json.fromJson(tInfo.getTaskParams(), BackupRequestParams.class))
          .filter(bRP -> bRP.backupUUID != null)
          .forEach(bRP -> backupParamsMap.put(bRP.getUniverseUUID(), bRP));
    }

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
                        Long universeSnapshotFilterTime = 0l;

                        if (backupParamsMap.containsKey(universe.getUniverseUUID())) {
                          UUID backupUUID =
                              backupParamsMap.get(universe.getUniverseUUID()).backupUUID;
                          Optional<Backup> oBackup = Backup.maybeGet(c.getUuid(), backupUUID);
                          if (oBackup.isPresent()) {
                            universeSnapshotFilterTime = oBackup.get().getCreateTime().getTime();
                          }
                        }
                        List<SnapshotInfo> snapshotInfoList = getNonScheduledSnapshotList(universe);
                        snapshotInfoList =
                            getFilterInProgressBackupSnapshots(
                                snapshotInfoList, universeSnapshotFilterTime);
                        if (CollectionUtils.isNotEmpty(snapshotInfoList)) {
                          String masterHostPorts = universe.getMasterAddresses();
                          String certificate = universe.getCertificateNodetoNode();
                          YBClient ybClient = ybService.getClient(masterHostPorts, certificate);
                          try {
                            snapshotInfoList.stream()
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
