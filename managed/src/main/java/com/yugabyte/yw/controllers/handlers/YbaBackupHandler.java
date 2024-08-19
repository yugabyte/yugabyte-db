/*
 * Copyright 2024 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import static com.yugabyte.yw.common.Util.NULL_UUID;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.CreateYbaBackup;
import com.yugabyte.yw.commissioner.tasks.RestoreContinuousBackup;
import com.yugabyte.yw.commissioner.tasks.RestoreYbaBackup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class YbaBackupHandler {

  @Inject private Commissioner commissioner;

  public UUID createBackup(Customer customer, CreateYbaBackup.Params taskParams) {
    UUID taskUUID = commissioner.submit(TaskType.CreateYbaBackup, taskParams);
    CustomerTask.create(
        customer,
        NULL_UUID,
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.CreateYbaBackup,
        "platform_ip");
    return taskUUID;
  }

  public UUID restoreBackup(Customer customer, RestoreYbaBackup.Params taskParams) {
    UUID taskUUID = commissioner.submit(TaskType.RestoreYbaBackup, taskParams);
    CustomerTask.create(
        customer,
        NULL_UUID,
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.RestoreYbaBackup,
        "platform_ip");
    return taskUUID;
  }

  public UUID restoreContinuousBackup(
      Customer customer, RestoreContinuousBackup.Params taskParams) {
    UUID taskUUID = commissioner.submit(TaskType.RestoreContinuousBackup, taskParams);
    CustomerTask.create(
        customer,
        NULL_UUID,
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.RestoreContinuousBackup,
        "platform_ip");
    return taskUUID;
  }
}
