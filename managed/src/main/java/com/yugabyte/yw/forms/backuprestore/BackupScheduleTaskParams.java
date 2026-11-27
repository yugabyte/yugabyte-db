// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.backuprestore;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import io.ebean.Model;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;

@Slf4j
@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = BackupScheduleTaskParams.Converter.class)
@Getter
@Setter
public class BackupScheduleTaskParams extends UpgradeTaskParams {
  public UUID scheduleUUID;
  public UUID customerUUID;
  public BackupRequestParams scheduleParams;

  /**
   * Create only: the schedule the request already saved in state Creating (the v2 create), which
   * the task sets up instead of creating one. Null for v1, where the task creates the schedule.
   */
  public UUID createdScheduleUUID;

  /**
   * Create only: check the storage config from YBA in a precheck subtask rather than when the task
   * is submitted, which runs in the request (the v2 create).
   */
  public boolean validateStorageConfigInTask;

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return true;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    if (this.scheduleParams.enablePointInTimeRestore
        && Util.isKubernetesBasedUniverse(universe.getUniverseDetails())) {
      String softwareVersion =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      if (!KubernetesUtil.isNonRestartGflagsUpgradeSupported(softwareVersion)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Software version %s does not support Point In Time Recovery enabled backup"
                    + " schedules",
                softwareVersion));
      }
    }
    verifyDbAPIsRunning(universe.getUniverseDetails().getPrimaryCluster().userIntent);
  }

  private void verifyDbAPIsRunning(UserIntent userIntent) {
    if (this.scheduleParams.backupType != null) {
      if (this.scheduleParams.backupType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)
          && !userIntent.enableYSQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YSQL tables if API is disabled");
      }
      if (this.scheduleParams.backupType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)
          && !userIntent.enableYCQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YCQL tables if API is disabled");
      }
    }
  }

  /**
   * The create task's checks when it is submitted, shared by CreateBackupSchedule and its
   * Kubernetes variant. {@code universe} is needed only on the first try. When the request already
   * saved the schedule, a failed check removes it (see {@link #removeCreatingSchedule}): a check
   * that fails when a queued task starts would otherwise leave it in Creating.
   */
  public void validateCreate(
      boolean isFirstTry,
      @Nullable Universe universe,
      BackupHelper backupHelper,
      CustomerConfigService customerConfigService) {
    try {
      if (createdScheduleUUID == null) {
        scheduleParams.validateExistingSchedule(customerUUID, isFirstTry);
      } else {
        if (!Schedule.maybeGet(customerUUID, createdScheduleUUID).isPresent()) {
          // A failed try removed it.
          throw new PlatformServiceException(
              BAD_REQUEST,
              "The backup schedule was removed when its create failed; create it again");
        }
        // The request's own schedule has the name; another one with it is a conflict.
        Schedule.maybeGetScheduleByUniverseWithName(
                scheduleParams.scheduleName, scheduleParams.getUniverseUUID(), customerUUID)
            .filter(existing -> !existing.getScheduleUUID().equals(createdScheduleUUID))
            .ifPresent(
                existing -> {
                  throw new PlatformServiceException(
                      BAD_REQUEST,
                      String.format(
                          "Schedule with same name %s already exists", existing.getScheduleName()));
                });
      }
      if (isFirstTry) {
        scheduleParams.validateScheduleParams(backupHelper, universe);
        boolean useStorageConfig =
            !validateStorageConfigInTask
                && !backupHelper.isSkipConfigBasedPreflightValidation(universe);
        scheduleParams.validateStorageConfigOnCreate(
            customerConfigService, backupHelper, useStorageConfig /* useConfig */, customerUUID);
      }
    } catch (RuntimeException e) {
      if (createdScheduleUUID != null) {
        removeCreatingSchedule(customerUUID, createdScheduleUUID);
      }
      throw e;
    }
  }

  /**
   * Removes a schedule the v2 create saved, when its create fails before the schedule is Active, so
   * that its name is free for a retry. The failed task says why. Only a schedule still in Creating
   * is removed. Never throws: the caller rethrows the create's own error.
   */
  public static void removeCreatingSchedule(UUID customerUUID, UUID scheduleUUID) {
    try {
      Schedule.maybeGet(customerUUID, scheduleUUID)
          .filter(schedule -> schedule.getStatus() == Schedule.State.Creating)
          .ifPresent(
              schedule -> {
                ScheduleTask.getAllTasks(scheduleUUID).forEach(Model::delete);
                schedule.delete();
                log.info("Removed backup schedule {} whose create failed", scheduleUUID);
              });
    } catch (RuntimeException e) {
      log.error("Could not remove backup schedule {} whose create failed", scheduleUUID, e);
    }
  }

  public static class Converter extends BaseConverter<BackupScheduleTaskParams> {}
}
