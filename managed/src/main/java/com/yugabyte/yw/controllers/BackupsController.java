// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

public class BackupsController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(BackupsController.class);

  @Inject
  FormFactory formFactory;

  @Inject
  Commissioner commissioner;

  public Result list(UUID customerUUID, UUID universeUUID) {
    List<Backup> backups = Backup.fetchByUniverseUUID(customerUUID, universeUUID);
    return ApiResponse.success(backups);
  }

  public Result restore(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      String errMsg = "Invalid Customer UUID: " + customerUUID;
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    try {
      Universe.get(universeUUID);
    } catch (RuntimeException re) {
      String errMsg = "Invalid Universe UUID: " + universeUUID;
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    Form<BackupTableParams> formData = formFactory.form(BackupTableParams.class)
        .bindFromRequest();

    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    BackupTableParams taskParams = formData.get();
    // Since we hit the restore endpoint, lets default the action type to RESTORE
    taskParams.actionType = BackupTableParams.ActionType.RESTORE;
    if (taskParams.storageLocation == null) {
      String errMsg = "Storage Location is required";
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    CustomerConfig storageConfig = CustomerConfig.get(customerUUID, taskParams.storageConfigUUID);
    if (storageConfig == null) {
      String errMsg = "Invalid StorageConfig UUID: " + taskParams.storageConfigUUID;
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }
    taskParams.universeUUID = universeUUID;

    Backup newBackup = Backup.create(customerUUID, taskParams);
    UUID taskUUID = commissioner.submit(TaskType.BackupUniverse, taskParams);
    LOG.info("Submitted task to restore table backup to {}.{}, task uuid = {}.",
        taskParams.keyspace, taskParams.tableName, taskUUID);
    newBackup.setTaskUUID(taskUUID);
    CustomerTask.create(customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Restore,
        taskParams.tableName);
    LOG.info("Saved task uuid {} in customer tasks table for table {}.{}", taskUUID,
        taskParams.keyspace, taskParams.tableName);

    ObjectNode resultNode = Json.newObject();
    resultNode.put("taskUUID", taskUUID.toString());
    return ApiResponse.success(resultNode);
  }
}
