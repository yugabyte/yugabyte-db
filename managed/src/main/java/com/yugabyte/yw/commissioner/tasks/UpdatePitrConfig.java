// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
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
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@Slf4j
@Abortable
@Retryable
public class UpdatePitrConfig extends UniverseTaskBase {

  private static long MAX_WAIT_TIME_MS = TimeUnit.MINUTES.toMillis(2);
  private static long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);
  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  protected UpdatePitrConfig(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
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
    if (taskParams().getKubernetesResourceDetails() != null) {
      pitrConfig.setKubernetesResourceDetails(taskParams().getKubernetesResourceDetails());
    }

    try (YBClient client = ybService.getUniverseClient(universe)) {
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

      // If retention period is being increased, freeze the current ERT till we have sufficient
      // number of snapshots to cater to the increased retention.
      long minRecoverTimeInMillisBeforeUpdate =
          pitrConfig.getRetentionPeriod() < taskParams().retentionPeriodInSeconds
              ? BackupUtil.getMinRecoveryTimeForSchedule(
                  scheduleListResp.getSnapshotScheduleInfoList().get(0).getSnapshotInfoList(),
                  pitrConfig)
              : 0L;

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
      pitrConfig.setIntermittentMinRecoverTimeInMillis(minRecoverTimeInMillisBeforeUpdate);
      pitrConfig.setUpdateTime(new Date());
      pitrConfig.update();

      log.debug("Successfully updated pitr config with uuid {}", pitrConfig.getUuid());
    } catch (Exception e) {
      pitrConfig.setState(State.FAILED);
      kubernetesStatus.updatePitrConfigStatus(pitrConfig, getName(), getUserTaskUUID());
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    pitrConfig.setState(State.COMPLETE);
    kubernetesStatus.updatePitrConfigStatus(pitrConfig, getName(), getUserTaskUUID());

    log.info("Completed {}", getName());
  }
}
