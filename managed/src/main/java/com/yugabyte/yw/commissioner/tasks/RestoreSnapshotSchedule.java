package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ListSnapshotRestorationsResponse;
import org.yb.client.RestoreSnapshotScheduleResponse;
import org.yb.client.SnapshotRestorationInfo;
import org.yb.client.YBClient;

@Slf4j
public class RestoreSnapshotSchedule extends UniverseTaskBase {

  private static final int WAIT_DURATION_IN_MS = 15000;

  @Inject
  protected RestoreSnapshotSchedule(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected RestoreSnapshotScheduleParams taskParams() {
    return (RestoreSnapshotScheduleParams) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().getUniverseUUID()
        + ", pitrConfigUUID="
        + taskParams().pitrConfigUUID
        + ")";
  }

  @Override
  public void run() {
    RestoreSnapshotScheduleResponse resp;
    YBClient client = null;
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try {
      log.info("Running {}: masterHostPorts={}.", getName(), masterHostPorts);

      client = ybService.getClient(masterHostPorts, certificate);
      resp =
          client.restoreSnapshotSchedule(
              taskParams().pitrConfigUUID, taskParams().restoreTimeInMillis);

      if (!resp.hasError()) {
        pollSnapshotRestorationTask(client, resp.getRestorationUUID());
      }
    } catch (Exception ex) {
      log.error("{} hit exception : {}", getName(), ex.getMessage());
      throw new RuntimeException(ex);
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }

    if (resp.hasError()) {
      String errorMsg = getName() + " failed due to error: " + resp.errorMessage();
      log.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }
  }

  private void pollSnapshotRestorationTask(YBClient client, UUID restorationUUID) {
    while (true) {
      waitFor(Duration.ofMillis(WAIT_DURATION_IN_MS));
      ListSnapshotRestorationsResponse listResp;
      try {
        listResp = client.listSnapshotRestorations(restorationUUID);
      } catch (Exception ex) {
        throw new RuntimeException(ex);
      }

      SnapshotRestorationInfo snapshotRestorationInfo = null;
      List<SnapshotRestorationInfo> snapshotRestorationList =
          listResp.getSnapshotRestorationInfoList();

      for (SnapshotRestorationInfo info : snapshotRestorationList) {
        if (info.getRestorationUUID().equals(restorationUUID)) {
          snapshotRestorationInfo = info;
          break;
        }
      }

      if (snapshotRestorationInfo == null) {
        throw new RuntimeException(
            String.format(
                "Restore snapshot response for restore uuid: %s is null", restorationUUID));
      }

      switch (snapshotRestorationInfo.getState()) {
        case RESTORING:
          break;
        case RESTORED:
          return;
        default:
          throw new RuntimeException(
              String.format(
                  "Restore snapshot schedule state for restore uuid: %s is invalid: %s",
                  restorationUUID, snapshotRestorationInfo.getState()));
      }
    }
  }
}
