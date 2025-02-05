// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import java.util.Date;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.client.EditSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.YBClient;

@Slf4j
@Abortable
public class UpdatePitrConfig extends UniverseTaskBase {

  private static long MAX_WAIT_TIME_MS = TimeUnit.MINUTES.toMillis(2);
  private static long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected UpdatePitrConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected UpdatePitrConfigParams taskParams() {
    return (UpdatePitrConfigParams) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(universeUuid=%s, customerUuid=%s, pitrConfigUuid=%s,"
            + " retentionPeriodInSeconds=%d, intervalInSeconds=%d)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().customerUUID,
        taskParams().pitrConfigUUID,
        taskParams().retentionPeriodInSeconds,
        taskParams().intervalInSeconds);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(taskParams().pitrConfigUUID);
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();

    try (YBClient client = ybService.getClient(masterAddresses, universeCertificate)) {
      ListSnapshotSchedulesResponse scheduleListResp =
          client.listSnapshotSchedules(pitrConfig.getUuid());
      if (!(CollectionUtils.size(scheduleListResp.getSnapshotScheduleInfoList()) == 1
          && scheduleListResp
              .getSnapshotScheduleInfoList()
              .get(0)
              .getSnapshotScheduleUUID()
              .equals(pitrConfig.getUuid()))) {
        throw new RuntimeException(
            String.format("Snapshot schedule with ID: %s was not found", pitrConfig.getUuid()));
      }

      EditSnapshotScheduleResponse resp =
          client.editSnapshotSchedule(
              pitrConfig.getUuid(),
              taskParams().retentionPeriodInSeconds,
              taskParams().intervalInSeconds);
      if (resp.hasError()) {
        String errorMsg = "Failed due to error: " + resp.errorMessage();
        log.error(errorMsg);
        throw new RuntimeException(errorMsg);
      }

      if (resp.getRetentionDurationInSecs() != taskParams().retentionPeriodInSeconds) {
        throw new RuntimeException(
            String.format(
                "Retention period of snapshot schedule returned: %s and expected: %s are different",
                resp.getRetentionDurationInSecs(), taskParams().retentionPeriodInSeconds));
      }

      if (resp.getIntervalInSecs() != taskParams().intervalInSeconds) {
        throw new RuntimeException(
            String.format(
                "Snapshot interval of schedule returned: %s and expected: %s are different",
                resp.getIntervalInSecs(), taskParams().intervalInSeconds));
      }

      pitrConfig.setRetentionPeriod(taskParams().retentionPeriodInSeconds);
      pitrConfig.setScheduleInterval(taskParams().intervalInSeconds);
      pitrConfig.setUpdateTime(new Date());
      pitrConfig.update();

      log.debug("Successfully updated pitr config with uuid {}", pitrConfig.getUuid());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
