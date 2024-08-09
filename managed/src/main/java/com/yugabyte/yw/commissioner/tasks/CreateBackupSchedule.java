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
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CreateBackupSchedule extends BackupScheduleBase {

  private final CustomerConfigService customerConfigService;
  private final YbcManager ybcManager;

  @Inject
  protected CreateBackupSchedule(
      BaseTaskDependencies baseTaskDependencies,
      YbcManager ybcManager,
      CustomerConfigService customerConfigService) {
    super(baseTaskDependencies);
    this.customerConfigService = customerConfigService;
    this.ybcManager = ybcManager;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams()
        .scheduleParams
        .validateExistingSchedule(isFirstTry, taskParams().getCustomerUUID());
    if (isFirstTry) {
      Universe universe = getUniverse();
      taskParams().scheduleParams.validateScheduleParams(backupHelper, universe);
      boolean useStorageConfig = !backupHelper.isSkipConfigBasedPreflightValidation(universe);
      taskParams()
          .scheduleParams
          .validateStorageConfigOnCreate(
              customerConfigService,
              backupHelper,
              useStorageConfig /* useConfig */,
              taskParams().getCustomerUUID());
    }
  }

  @Override
  public void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    backupHelper.validateBackupRequest(
        taskParams().scheduleParams.keyspaceTableList,
        universe,
        taskParams().scheduleParams.backupType);
  }

  @Override
  public void run() {
    addAllCreateBackupScheduleTasks(
        getBackupScheduleUniverseSubtasks(
            getUniverse(), taskParams().scheduleParams, false /* isDelete */),
        taskParams().scheduleParams,
        taskParams().customerUUID,
        ybcManager.getStableYbcVersion());
  }
}
