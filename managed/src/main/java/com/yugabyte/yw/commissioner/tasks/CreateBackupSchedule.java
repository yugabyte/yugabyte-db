/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;

@Slf4j
public class CreateBackupSchedule extends UniverseTaskBase {

  private final CustomerConfigService customerConfigService;
  private final StorageUtilFactory storageUtilFactory;
  private final YbcManager ybcManager;

  @Inject
  protected CreateBackupSchedule(
      BaseTaskDependencies baseTaskDependencies,
      CustomerConfigService customerConfigService,
      StorageUtilFactory storageUtilFactory,
      YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.customerConfigService = customerConfigService;
    this.storageUtilFactory = storageUtilFactory;
    this.ybcManager = ybcManager;
  }

  protected BackupRequestParams params() {
    return (BackupRequestParams) taskParams;
  }

  @Override
  public void run() {
    BackupRequestParams taskParams = params();

    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());
    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(
            taskParams.customerUUID, taskParams.storageConfigUUID);
    boolean ybcBackup =
        !BackupCategory.YB_BACKUP_SCRIPT.equals(params().backupCategory)
            && universe.isYbcEnabled()
            && !params().backupType.equals(TableType.REDIS_TABLE_TYPE);

    if (ybcBackup
        && universe.isYbcEnabled()
        && !universe
            .getUniverseDetails()
            .getYbcSoftwareVersion()
            .equals(ybcManager.getStableYbcVersion())) {

      if (universe
          .getUniverseDetails()
          .getPrimaryCluster()
          .userIntent
          .providerType
          .equals(Common.CloudType.kubernetes)) {
        createUpgradeYbcTaskOnK8s(params().getUniverseUUID(), ybcManager.getStableYbcVersion())
            .setSubTaskGroupType(SubTaskGroupType.UpgradingYbc);
      } else {
        createUpgradeYbcTask(params().getUniverseUUID(), ybcManager.getStableYbcVersion(), true)
            .setSubTaskGroupType(SubTaskGroupType.UpgradingYbc);
      }
    }
    createPreflightValidateBackupTask(
        taskParams.storageConfigUUID,
        taskParams.customerUUID,
        taskParams.getUniverseUUID(),
        ybcBackup);

    Schedule schedule =
        Schedule.create(
            taskParams.customerUUID,
            taskParams.getUniverseUUID(),
            taskParams,
            TaskType.CreateBackup,
            taskParams.schedulingFrequency,
            taskParams.cronExpression,
            taskParams.frequencyTimeUnit,
            taskParams.scheduleName);
    UUID scheduleUUID = schedule.getScheduleUUID();
    log.info(
        "Created backup schedule for customer {}, schedule uuid = {}.",
        taskParams.customerUUID,
        scheduleUUID);
  }
}
