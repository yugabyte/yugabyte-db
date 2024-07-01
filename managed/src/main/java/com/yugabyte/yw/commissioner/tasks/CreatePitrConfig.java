// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.backuprestore.BackupUtil.TABLE_TYPE_TO_YQL_DATABASE_MAP;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.CreateSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.ListSnapshotsResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@Slf4j
@Abortable
public class CreatePitrConfig extends UniverseTaskBase {
  public static final List<State> SNAPSHOT_VALID_STATES =
      ImmutableList.of(State.CREATING, State.COMPLETE);

  @Inject
  protected CreatePitrConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected CreatePitrConfigParams taskParams() {
    return (CreatePitrConfigParams) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUuid=%s,tableType=%s,keyspaceName=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().tableType,
        taskParams().keyspaceName);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(masterAddresses, universeCertificate)) {
      // Find the keyspace id of the keyspace name specified in the task params.
      String keyspaceId =
          getKeyspaceNameKeyspaceIdMap(client, taskParams().tableType)
              .get(taskParams().keyspaceName);
      if (Objects.isNull(keyspaceId)) {
        throw new IllegalArgumentException(
            String.format(
                "A keyspace with name %s and table type %s could not be found",
                taskParams().keyspaceName, taskParams().tableType));
      }
      log.debug("Found keyspace id {} for keyspace name {}", keyspaceId, taskParams().keyspaceName);

      // Create the PITR config on DB.
      CreateSnapshotScheduleResponse resp =
          client.createSnapshotSchedule(
              TABLE_TYPE_TO_YQL_DATABASE_MAP.get(taskParams().tableType),
              taskParams().keyspaceName,
              keyspaceId,
              taskParams().retentionPeriodInSeconds,
              taskParams().intervalInSeconds);
      if (resp.hasError()) {
        String errorMsg = getName() + " failed due to error: " + resp.errorMessage();
        log.error(errorMsg);
        throw new RuntimeException(errorMsg);
      }
      UUID snapshotScheduleUuid = resp.getSnapshotScheduleUUID();
      PitrConfig pitrConfig = PitrConfig.create(snapshotScheduleUuid, taskParams());
      if (Objects.nonNull(taskParams().xClusterConfig)) {
        // This PITR config is created as part of an xCluster config.
        taskParams().xClusterConfig.addPitrConfig(pitrConfig);
      }

      Duration pitrCreatePollDelay =
          confGetter.getConfForScope(universe, UniverseConfKeys.pitrCreatePollDelay);
      long pitrCreatePollDelayMs = pitrCreatePollDelay.toMillis();
      long pitrCreateTimeoutMs =
          confGetter.getConfForScope(universe, UniverseConfKeys.pitrCreateTimeout).toMillis();
      long startTime = System.currentTimeMillis();

      // Get the snapshot info object.
      AtomicReference<SnapshotInfo> snapshotInfoAtomicRef = new AtomicReference<>(null);
      doWithConstTimeout(
          pitrCreatePollDelayMs,
          pitrCreateTimeoutMs,
          () -> {
            try {
              ListSnapshotSchedulesResponse scheduleResp =
                  client.listSnapshotSchedules(snapshotScheduleUuid);
              SnapshotInfo snapshotInfo =
                  getSnapshotInfo(snapshotScheduleUuid, scheduleResp.getSnapshotScheduleInfoList());
              snapshotInfoAtomicRef.set(snapshotInfo);
            } catch (Exception e) {
              throw new RuntimeException(e);
            }
          });

      // Poll to ensure the snapshot is in COMPLETE state.
      UUID snapshotUuid = snapshotInfoAtomicRef.get().getSnapshotUUID();
      long remainingTimeoutMs = pitrCreateTimeoutMs - (System.currentTimeMillis() - startTime);
      doWithConstTimeout(
          pitrCreatePollDelayMs,
          remainingTimeoutMs,
          () -> {
            try {
              ListSnapshotsResponse listSnapshotsResponse =
                  client.listSnapshots(snapshotUuid, true);
              List<SnapshotInfo> snapshotInfoList = listSnapshotsResponse.getSnapshotInfoList();
              SnapshotInfo snapshotInfo =
                  snapshotInfoList.stream()
                      .filter(si -> snapshotUuid.equals(si.getSnapshotUUID()))
                      .findFirst()
                      .orElseThrow(
                          () ->
                              new RuntimeException(
                                  String.format(
                                      "Snapshot with uuid %s not found in listSnapshots response"
                                          + " %s",
                                      snapshotUuid, snapshotInfoList)));
              if (State.COMPLETE.equals(snapshotInfo.getState())) {
                return;
              }
              if (State.CREATING.equals(snapshotInfo.getState())) {
                throw new RuntimeException("Snapshot is still in CREATING state");
              }
              // Snapshot is in an invalid state.
              throw new RuntimeException(
                  String.format(
                      "Valid states for a snapshot are %s, but the snapshot is in an invalid"
                          + " state: %s",
                      SNAPSHOT_VALID_STATES, snapshotInfo.getState()));
            } catch (Exception e) {
              throw new RuntimeException(e);
            }
          });
    } catch (Exception e) {
      log.error("{} hit exception : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }

  public static SnapshotInfo getSnapshotInfo(
      UUID snapshotScheduleUUID, List<SnapshotScheduleInfo> scheduleInfoList) {
    if (Objects.isNull(scheduleInfoList)) {
      throw new RuntimeException(
          String.format("Snapshot schedules returned null for schedule: %s", snapshotScheduleUUID));
    }
    if (scheduleInfoList.size() != 1) {
      throw new RuntimeException(
          String.format(
              "Snapshot schedules returned %s entries for schedule: %s. One entry is expected",
              scheduleInfoList.size(), snapshotScheduleUUID));
    }
    SnapshotScheduleInfo snapshotScheduleInfo = scheduleInfoList.get(0);
    if (!snapshotScheduleInfo.getSnapshotScheduleUUID().equals(snapshotScheduleUUID)) {
      throw new RuntimeException(
          String.format(
              "Snapshot schedule returned: %s and expected: %s are different",
              snapshotScheduleInfo.getSnapshotScheduleUUID(), snapshotScheduleUUID));
    }
    List<SnapshotInfo> snapshotInfoList = snapshotScheduleInfo.getSnapshotInfoList();
    if (Objects.isNull(snapshotInfoList)) {
      throw new RuntimeException(
          "Snapshot list returned as null for the schedule: " + snapshotScheduleUUID);
    }
    if (snapshotInfoList.size() != 1) {
      throw new RuntimeException(
          String.format(
              "Snapshot list returned %s entries for schedule: %s. One entry is expected",
              snapshotInfoList.size(), snapshotScheduleUUID));
    }
    return snapshotInfoList.get(0);
  }
}
