// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.backuprestore.BackupUtil.TABLE_TYPE_TO_YQL_DATABASE_MAP;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.Date;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.CreateSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.ListSnapshotsResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@Slf4j
@Abortable
@Retryable
public class CreatePitrConfig extends UniverseTaskBase {
  public static final List<State> SNAPSHOT_VALID_STATES =
      ImmutableList.of(State.CREATING, State.COMPLETE);
  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  protected CreatePitrConfig(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
  }

  @Override
  protected CreatePitrConfigParams taskParams() {
    return (CreatePitrConfigParams) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUuid=%s, customerUuid=%s, name=%s, keyspaceName=%s, tableType=%s,"
            + " retentionPeriodInSeconds=%s, intervalInSeconds=%d, xClusterConfig=%s,"
            + " createdForDr=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().customerUUID,
        taskParams().name,
        taskParams().keyspaceName,
        taskParams().tableType,
        taskParams().retentionPeriodInSeconds,
        taskParams().intervalInSeconds,
        taskParams().xClusterConfig,
        taskParams().createdForDr);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    try (YBClient client = ybService.getUniverseClient(universe)) {
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

      UUID snapshotScheduleUuid =
          findExistingSchedule(
              client,
              taskParams().keyspaceName,
              TABLE_TYPE_TO_YQL_DATABASE_MAP.get(taskParams().tableType),
              taskParams().intervalInSeconds,
              taskParams().retentionPeriodInSeconds);

      if (snapshotScheduleUuid == null) {
        // Create the PITR config on DB (no matching schedule found).
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
        snapshotScheduleUuid = resp.getSnapshotScheduleUUID();
        log.info("Created new snapshot schedule {}", snapshotScheduleUuid);
      } else {
        log.info(
            "Reusing existing snapshot schedule {} for keyspace={}, dbType={}, interval={},"
                + " retention={}",
            snapshotScheduleUuid,
            taskParams().keyspaceName,
            TABLE_TYPE_TO_YQL_DATABASE_MAP.get(taskParams().tableType),
            taskParams().intervalInSeconds,
            taskParams().retentionPeriodInSeconds);
      }

      final UUID scheduleUuidForPolling = snapshotScheduleUuid;

      // Avoid duplicate PitrConfig rows: reuse if already present.
      Optional<PitrConfig> existing = PitrConfig.maybeGet(scheduleUuidForPolling);
      PitrConfig pitrConfig;
      if (existing.isPresent()) {
        pitrConfig = existing.get();
        log.info("PitrConfig already exists for schedule {}. Reusing.", scheduleUuidForPolling);
      } else {
        pitrConfig = PitrConfig.create(scheduleUuidForPolling, taskParams());
      }
      if (taskParams().getKubernetesResourceDetails() != null) {
        pitrConfig.setKubernetesResourceDetails(taskParams().getKubernetesResourceDetails());
      }
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
                  client.listSnapshotSchedules(scheduleUuidForPolling);
              SnapshotInfo snapshotInfo =
                  getSnapshotInfo(
                      scheduleUuidForPolling, scheduleResp.getSnapshotScheduleInfoList());
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
              pitrConfig.setState(snapshotInfo.getState());
              kubernetesStatus.updatePitrConfigStatus(pitrConfig, getName(), getUserTaskUUID());
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
              kubernetesStatus.updatePitrConfigStatus(pitrConfig, getName(), getUserTaskUUID());
              throw new RuntimeException(e);
            }
          });
      // Adding 1s to the snapshot creation time to take care of loss of precision when converted
      // from micros to millis.
      Date currentDate = new Date(snapshotInfoAtomicRef.get().getSnapshotTime() + 1000L);
      pitrConfig.setCreateTime(currentDate);
      pitrConfig.setUpdateTime(currentDate);
      pitrConfig.save();
    } catch (Exception e) {
      log.error("{} hit exception : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }

  private UUID findExistingSchedule(
      YBClient client,
      String keyspaceName,
      YQLDatabase dbType,
      long intervalSeconds,
      long retentionSeconds)
      throws Exception {

    ListSnapshotSchedulesResponse all = client.listSnapshotSchedules(null);
    for (SnapshotScheduleInfo s : all.getSnapshotScheduleInfoList()) {
      String nsName = s.getNamespaceName();
      YQLDatabase sDbType = s.getDbType();

      if (nsName == null) {
        continue;
      }

      if (keyspaceName.equals(nsName)
          && sDbType == dbType
          && s.getIntervalInSecs() == intervalSeconds
          && s.getRetentionDurationInSecs() == retentionSeconds) {
        return s.getSnapshotScheduleUUID();
      }
    }

    return null;
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
