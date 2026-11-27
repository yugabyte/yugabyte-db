/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
public class CreateBackupScheduleKubernetes extends BackupScheduleBaseKubernetes {

  private final CustomerConfigService customerConfigService;
  private final YbcManager ybcManager;

  @Inject
  protected CreateBackupScheduleKubernetes(
      BaseTaskDependencies baseTaskDependencies,
      YbcManager ybcManager,
      CustomerConfigService customerConfigService,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
    this.ybcManager = ybcManager;
    this.customerConfigService = customerConfigService;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams()
        .validateCreate(
            isFirstTry, isFirstTry ? getUniverse() : null, backupHelper, customerConfigService);
  }

  @Override
  public void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    backupHelper.validateBackupRequest(
        taskParams().scheduleParams.keyspaceTableList,
        universe,
        taskParams().scheduleParams.backupType);
    if (taskParams().validateStorageConfigInTask) {
      // From YBA only: run() checks the storage from the universe for a YB-Controller backup.
      createBackupStorageConfigValidateTask(
              taskParams().scheduleParams.storageConfigUUID,
              taskParams().getCustomerUUID(),
              universe.getUniverseUUID(),
              false /* ybcBackup */)
          .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);
    }
  }

  @Override
  public void run() {
    addAllCreateBackupScheduleTasks(
        getBackupScheduleUniverseSubtasks(
            getUniverse(), taskParams().scheduleParams, null /* deleteScheduleUUID */),
        taskParams().scheduleParams,
        taskParams().customerUUID,
        ybcManager.getStableYbcVersion(),
        taskParams().createdScheduleUUID);
  }
}
