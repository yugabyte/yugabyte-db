// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackup;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class BackupsController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(BackupsController.class);

  @Inject Commissioner commissioner;

  public Result list(UUID customerUUID, UUID universeUUID) {
    List<Backup> backups = Backup.fetchByUniverseUUID(customerUUID, universeUUID);
    JsonNode custStorageLoc =
        CommonUtils.getNodeProperty(
            Customer.get(customerUUID).getFeatures(), "universes.details.backups.storageLocation");
    boolean isStorageLocMasked = custStorageLoc != null && custStorageLoc.asText().equals("hidden");
    if (!isStorageLocMasked) {
      Users user = (Users) ctx().args.get("user");
      JsonNode userStorageLoc =
          CommonUtils.getNodeProperty(
              user.getFeatures(), "universes.details.backups.storageLocation");
      isStorageLocMasked = userStorageLoc != null && userStorageLoc.asText().equals("hidden");
    }

    // If either customer or user featureConfig has storageLocation hidden,
    // mask the string in each backup
    if (isStorageLocMasked) {
      for (Backup backup : backups) {
        BackupTableParams params = backup.getBackupInfo();
        String loc = params.storageLocation;
        if (!loc.isEmpty()) {
          params.storageLocation = "**********";
        }
        backup.setBackupInfo(params);
      }
    }
    return YWResults.withData(backups);
  }

  public Result restore(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    Form<BackupTableParams> formData = formFactory.getFormDataOrBadRequest(BackupTableParams.class);

    BackupTableParams taskParams = formData.get();
    // Since we hit the restore endpoint, lets default the action type to RESTORE
    taskParams.actionType = BackupTableParams.ActionType.RESTORE;
    if (taskParams.storageLocation == null && taskParams.backupList == null) {
      String errMsg = "Storage Location is required";
      throw new YWServiceException(BAD_REQUEST, errMsg);
    }

    taskParams.universeUUID = universeUUID;

    // Change the BackupTableParams in list to be "RESTORE" action type
    if (taskParams.backupList != null) {
      for (BackupTableParams subParams : taskParams.backupList) {
        // Override default CREATE action type that we inherited from backup flow
        subParams.actionType = BackupTableParams.ActionType.RESTORE;
        // Assume no renaming of keyspaces or tables
        subParams.tableUUIDList = null;
        subParams.tableNameList = null;
        subParams.tableUUID = null;
        subParams.setTableName(null);
        subParams.setKeyspace(null);
        subParams.universeUUID = universeUUID;
        subParams.parallelism = taskParams.parallelism;
      }
    }
    CustomerConfig storageConfig =
        CustomerConfig.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (taskParams.getTableName() != null && taskParams.getKeyspace() == null) {
      throw new YWServiceException(BAD_REQUEST, "Restore table request must specify keyspace.");
    }

    Backup newBackup = Backup.create(customerUUID, taskParams);
    UUID taskUUID = commissioner.submit(TaskType.BackupUniverse, taskParams);
    LOG.info(
        "Submitted task to RESTORE table backup to {}.{} with config {} from {}, task uuid = {}.",
        taskParams.getKeyspace(),
        taskParams.getTableName(),
        storageConfig.configName,
        taskParams.storageLocation,
        taskUUID);
    newBackup.setTaskUUID(taskUUID);
    if (taskParams.getTableName() != null) {
      CustomerTask.create(
          customer,
          universeUUID,
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Restore,
          taskParams.getTableName());
      LOG.info(
          "Saved task uuid {} in customer tasks table for table {}.{}",
          taskUUID,
          taskParams.getKeyspace(),
          taskParams.getTableName());
    } else if (taskParams.getKeyspace() != null) {
      CustomerTask.create(
          customer,
          universeUUID,
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Restore,
          taskParams.getKeyspace());
      LOG.info(
          "Saved task uuid {} in customer tasks table for keyspace {}",
          taskUUID,
          taskParams.getKeyspace());
    } else {
      CustomerTask.create(
          customer,
          universeUUID,
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Restore,
          universe.name);
      if (taskParams.backupList != null) {
        LOG.info(
            "Saved task uuid {} in customer tasks table for universe backup {}",
            taskUUID,
            universe.name);
      } else {
        LOG.info(
            "Saved task uuid {} in customer tasks table for restore identical "
                + "keyspace & tables in universe {}",
            taskUUID,
            universe.name);
      }
    }

    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()), taskUUID);
    return new YWResults.YWTask(taskUUID).asResult();
  }

  public Result delete(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // TODO(API): Let's get rid of raw Json.
    // Create DeleteBackupReq in form package and bind to that
    ObjectNode formData = (ObjectNode) request().body().asJson();
    List<UUID> taskUUIDList = new ArrayList<>();
    for (JsonNode backupUUID : formData.get("backupUUID")) {
      UUID uuid = UUID.fromString(backupUUID.asText());
      Backup backup = Backup.get(customerUUID, uuid);
      if (backup == null) {
        LOG.info(
            "Can not delete {} backup as it is not present in the database.", backupUUID.asText());
      } else {
        if (backup.state != Backup.BackupState.Completed) {
          LOG.info("Can not delete {} backup as it is still in progress", uuid);
        } else {
          DeleteBackup.Params taskParams = new DeleteBackup.Params();
          taskParams.customerUUID = customerUUID;
          taskParams.backupUUID = uuid;
          UUID taskUUID = commissioner.submit(TaskType.DeleteBackup, taskParams);
          LOG.info("Saved task uuid {} in customer tasks for backup {}.", taskUUID, uuid);
          CustomerTask.create(
              customer,
              backup.getBackupInfo().universeUUID,
              taskUUID,
              CustomerTask.TargetType.Backup,
              CustomerTask.TaskType.Delete,
              "Backup");
          taskUUIDList.add(taskUUID);
          auditService().createAuditEntry(ctx(), request(), taskUUID);
        }
      }
    }
    return new YWResults.YWTasks(taskUUIDList).asResult();
  }
}
