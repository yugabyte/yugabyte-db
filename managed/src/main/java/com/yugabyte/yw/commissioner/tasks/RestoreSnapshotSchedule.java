package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.time.Duration;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.SnapshotInfo;
import org.yb.client.RestoreSnapshotScheduleResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

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
        + taskParams().universeUUID
        + ", pitrConfigUUID="
        + taskParams().pitrConfigUUID
        + ")";
  }

  @Override
  public void run() {
    RestoreSnapshotScheduleResponse resp;
    YBClient client = null;
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    try {
      log.info("Running {}: masterHostPorts={}.", getName(), masterHostPorts);

      client = ybService.getClient(masterHostPorts, certificate);
      resp =
          client.restoreSnapshotSchedule(
              taskParams().pitrConfigUUID, taskParams().restoreTimeInMillis);

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
}
