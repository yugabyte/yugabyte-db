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
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CreateBackupSchedule extends UniverseTaskBase {

  private final BackupUtil backupUtil;
  private final CustomerConfigService customerConfigService;

  @Inject
  protected CreateBackupSchedule(
      BaseTaskDependencies baseTaskDependencies,
      CustomerConfigService customerConfigService,
      BackupUtil backupUtil) {
    super(baseTaskDependencies);
    this.backupUtil = backupUtil;
    this.customerConfigService = customerConfigService;
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
    StorageUtil.getStorageUtil(customerConfig.getName())
        .validateStorageConfigOnUniverse(customerConfig, universe);

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
