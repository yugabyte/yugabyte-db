package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.BackupUtil.TABLE_TYPE_TO_YQL_DATABASE_MAP;
import static com.yugabyte.yw.common.Util.getUUIDRepresentation;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;
import java.lang.StringBuilder;
import java.time.Duration;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import org.yb.client.CreateSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.ListSnapshotsResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@Slf4j
@Abortable
public class CreatePitrConfig extends UniverseTaskBase {

  private Set<State> ACCEPTED_STATES = new HashSet<>(Arrays.asList(State.CREATING, State.COMPLETE));
  private static final int WAIT_DURATION_IN_MS = 15000;

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
    return super.getName()
        + "("
        + taskParams().universeUUID
        + ", keyspaceName="
        + taskParams().keyspaceName
        + ")";
  }

  @Override
  public void run() {
    CreateSnapshotScheduleResponse resp;
    YBClient client = null;
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try {
      log.info("Running {}: masterHostPorts={}.", getName(), masterHostPorts);

      client = ybService.getClient(masterHostPorts, certificate);
      resp =
          client.createSnapshotSchedule(
              TABLE_TYPE_TO_YQL_DATABASE_MAP.get(taskParams().tableType),
              taskParams().keyspaceName,
              taskParams().retentionPeriodInSeconds,
              taskParams().intervalInSeconds);

      if (!resp.hasError()) {
        UUID snapshotScheduleUUID = resp.getSnapshotScheduleUUID();
        PitrConfig.create(snapshotScheduleUUID, taskParams());
        waitFor(Duration.ofMillis(WAIT_DURATION_IN_MS));
        ListSnapshotSchedulesResponse scheduleResp =
            client.listSnapshotSchedules(snapshotScheduleUUID);
        List<SnapshotScheduleInfo> scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
        SnapshotInfo latestSnapshot =
            validateSnapshotSchedule(snapshotScheduleUUID, scheduleInfoList);
        pollSnapshotCreationTask(client, latestSnapshot.getSnapshotUUID());
      }
    } catch (Exception e) {
      log.error("{} hit exception : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }

    if (resp.hasError()) {
      String errorMsg = getName() + " failed due to error: " + resp.errorMessage();
      log.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }
  }

  private SnapshotInfo validateSnapshotSchedule(
      UUID snapshotScheduleUUID, List<SnapshotScheduleInfo> scheduleInfoList) {
    if (scheduleInfoList == null) {
      throw new RuntimeException(
          "Snapshot schedules returned null for schedule: " + snapshotScheduleUUID.toString());
    } else if (scheduleInfoList.size() != 1) {
      throw new RuntimeException(
          String.format(
              "Snapshot schedules returned %s entries for schedule: %s. One entry is expected",
              String.valueOf(scheduleInfoList.size()), snapshotScheduleUUID));
    }

    SnapshotScheduleInfo snapshotScheduleInfo = scheduleInfoList.get(0);

    if (!snapshotScheduleInfo.getSnapshotScheduleUUID().equals(snapshotScheduleUUID)) {
      throw new RuntimeException(
          String.format(
              "Snapshot schedule returned: %s and expected: %s are different",
              snapshotScheduleInfo.getSnapshotScheduleUUID().toString(),
              snapshotScheduleUUID.toString()));
    }

    List<SnapshotInfo> snapshotInfoList = snapshotScheduleInfo.getSnapshotInfoList();
    if (snapshotInfoList == null) {
      throw new RuntimeException(
          "Snapshot list returned as null for the schedule: " + snapshotScheduleUUID.toString());
    } else if (snapshotInfoList.size() != 1) {
      throw new RuntimeException(
          String.format(
              "Snapshot list returned %s entries for schedule: %s. One entry is expected",
              String.valueOf(scheduleInfoList.size()), snapshotScheduleUUID.toString()));
    }

    SnapshotInfo latestSnapshotInfo = snapshotInfoList.get(0);
    if (!ACCEPTED_STATES.contains(latestSnapshotInfo.getState())) {
      throw new RuntimeException(
          String.format(
              "Snapshot: %s is in incorrect state: %s",
              latestSnapshotInfo.getSnapshotUUID().toString(),
              latestSnapshotInfo.getState().toString()));
    }
    return latestSnapshotInfo;
  }

  private void pollSnapshotCreationTask(YBClient client, UUID snapshotUUID) {
    while (true) {
      ListSnapshotsResponse listSnapshotsResponse;
      try {
        listSnapshotsResponse = client.listSnapshots(snapshotUUID, true);
      } catch (Exception ex) {
        throw new RuntimeException(ex);
      }

      SnapshotInfo snapshotInfo = null;
      List<SnapshotInfo> snapshotInfoList = listSnapshotsResponse.getSnapshotInfoList();
      for (SnapshotInfo info : snapshotInfoList) {
        if (info.getSnapshotUUID().equals(snapshotUUID)) {
          snapshotInfo = info;
          break;
        }
      }
      if (snapshotInfo == null) {
        throw new RuntimeException();
      }
      switch (snapshotInfo.getState()) {
        case CREATING:
          break;
        case COMPLETE:
          return;
        default:
          throw new RuntimeException();
      }
      waitFor(Duration.ofMillis(WAIT_DURATION_IN_MS));
    }
  }
}
