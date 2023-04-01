// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.DeleteSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;

@Slf4j
public class DeletePitrConfig extends UniverseTaskBase {

  @Inject
  protected DeletePitrConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    // The universe UUID must be stored in universeUUID field.

    // The UUID of the PITR config to delete.
    public UUID pitrConfigUuid;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(Universe=%s,pitrConfigUuid=%s)",
        super.getName(), taskParams().getUniverseUUID(), taskParams().pitrConfigUuid);
  }

  @Override
  public void run() {
    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(taskParams().pitrConfigUuid);

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      ListSnapshotSchedulesResponse scheduleListResp = client.listSnapshotSchedules(null);
      for (SnapshotScheduleInfo scheduleInfo : scheduleListResp.getSnapshotScheduleInfoList()) {
        if (scheduleInfo.getSnapshotScheduleUUID().equals(pitrConfig.getUuid())) {
          DeleteSnapshotScheduleResponse resp = client.deleteSnapshotSchedule(pitrConfig.getUuid());
          if (resp.hasError()) {
            String errorMsg = "Failed due to error: " + resp.errorMessage();
            log.error(errorMsg);
            throw new RuntimeException(errorMsg);
          }
        }
      }

      pitrConfig.delete();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }
}
