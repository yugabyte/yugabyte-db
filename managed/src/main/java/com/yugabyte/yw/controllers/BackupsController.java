// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackup;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TaskInfoManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.PlatformResults.YBPTasks;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

@Api(value = "Backups", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class BackupsController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(BackupsController.class);
  private static final int maxRetryCount = 5;

  private final Commissioner commissioner;
  private final CustomerConfigService customerConfigService;

  @Inject
  public BackupsController(Commissioner commissioner, CustomerConfigService customerConfigService) {
    this.commissioner = commissioner;
    this.customerConfigService = customerConfigService;
  }

  @Inject TaskInfoManager taskManager;

  @ApiOperation(
      value = "List a customer's backups",
      response = Backup.class,
      responseContainer = "List",
      nickname = "ListOfBackups")
  @ApiResponses(
      @io.swagger.annotations.ApiResponse(
          code = 500,
          message = "If there was a server or database issue when listing the backups",
          response = YBPError.class))
  public Result list(UUID customerUUID, UUID universeUUID) {
    List<Backup> backups = Backup.fetchByUniverseUUID(customerUUID, universeUUID);
    JsonNode custStorageLoc =
        CommonUtils.getNodeProperty(
            Customer.get(customerUUID).getFeatures(), "universes.details.backups.storageLocation");
    boolean isStorageLocMasked = custStorageLoc != null && custStorageLoc.asText().equals("hidden");
    if (!isStorageLocMasked) {
      UserWithFeatures user = (UserWithFeatures) ctx().args.get("user");
      JsonNode userStorageLoc =
          CommonUtils.getNodeProperty(
              user.getFeatures(), "universes.details.backups.storageLocation");
      isStorageLocMasked = userStorageLoc != null && userStorageLoc.asText().equals("hidden");
    }

    // If either customer or user featureConfig has storageLocation hidden,
    // mask the string in each backup.
    if (isStorageLocMasked) {
      for (Backup backup : backups) {
        BackupTableParams params = backup.getBackupInfo();
        String loc = params.storageLocation;
        if ((loc != null) && !loc.isEmpty()) {
          params.storageLocation = "**********";
        }
        backup.setBackupInfo(params);
      }
    }
    return PlatformResults.withData(backups);
  }

  @ApiOperation(
      value = "List a task's backups",
      response = Backup.class,
      responseContainer = "List")
  @ApiResponses(
      @io.swagger.annotations.ApiResponse(
          code = 500,
          message = "If there was a server or database issue when listing the backups",
          response = YBPError.class))
  public Result fetchBackupsByTaskUUID(UUID customerUUID, UUID universeUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID);

    List<Backup> backups = Backup.fetchAllBackupsByTaskUUID(taskUUID);
    return PlatformResults.withData(backups);
  }

  @ApiOperation(
      value = "Restore from a backup",
      response = YBPTask.class,
      responseContainer = "Restore")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "backup",
          value = "Parameters of the backup to be restored",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.BackupTableParams",
          required = true))
  public Result restore(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    Form<BackupTableParams> formData = formFactory.getFormDataOrBadRequest(BackupTableParams.class);

    BackupTableParams taskParams = formData.get();
    // Since we hit the restore endpoint, lets default the action type to RESTORE
    taskParams.actionType = BackupTableParams.ActionType.RESTORE;
    if (taskParams.storageLocation == null && taskParams.backupList == null) {
      String errMsg = "Storage Location is required";
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    taskParams.universeUUID = universeUUID;
    taskParams.customerUuid = customerUUID;

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
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (taskParams.getTableName() != null && taskParams.getKeyspace() == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Restore table request must specify keyspace.");
    }

    UUID taskUUID = commissioner.submit(TaskType.BackupUniverse, taskParams);
    LOG.info(
        "Submitted task to RESTORE table backup to {}.{} with config {} from {}, task uuid = {}.",
        taskParams.getKeyspace(),
        taskParams.getTableName(),
        storageConfig.configName,
        taskParams.storageLocation,
        taskUUID);
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
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(value = "Delete backups", response = YBPTasks.class, nickname = "deleteBackups")
  public Result delete(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // TODO(API): Let's get rid of raw Json.
    // Create DeleteBackupReq in form package and bind to that
    ObjectNode formData = (ObjectNode) request().body().asJson();
    List<YBPTask> taskList = new ArrayList<>();
    for (JsonNode backupUUID : formData.get("backupUUID")) {
      UUID uuid = UUID.fromString(backupUUID.asText());
      Backup backup = Backup.get(customerUUID, uuid);
      if (backup == null) {
        LOG.info(
            "Can not delete {} backup as it is not present in the database.", backupUUID.asText());
      } else {
        if (backup.state != Backup.BackupState.Completed
            && backup.state != Backup.BackupState.Failed) {
          LOG.info("Can not delete {} backup as it is still in progress", uuid);
        } else {
          if (taskManager.isDuplicateDeleteBackupTask(customerUUID, uuid)) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Task to delete same backup already exists.");
          }

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
          taskList.add(new YBPTask(taskUUID, taskParams.backupUUID));
          auditService().createAuditEntry(ctx(), request(), taskUUID);
        }
      }
    }
    return new YBPTasks(taskList).asResult();
  }

  @ApiOperation(
      value = "Stop a backup",
      notes = "Stop an in-progress backup",
      nickname = "stopBackup")
  public Result stop(UUID customerUUID, UUID backupUUID) {
    Customer.getOrBadRequest(customerUUID);
    Process process = Util.getProcessOrBadRequest(backupUUID);
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    if (backup.state != Backup.BackupState.InProgress) {
      LOG.info("The backup {} you are trying to stop is not in progress.", backupUUID);
      throw new PlatformServiceException(
          BAD_REQUEST, "The backup you are trying to stop is not in process.");
    }
    if (process == null) {
      LOG.info("The backup {} process you want to stop doesn't exist.", backupUUID);
      throw new PlatformServiceException(
          BAD_REQUEST, "The backup process you want to stop doesn't exist.");
    } else {
      process.destroyForcibly();
    }
    Util.removeProcess(backupUUID);
    try {
      waitForTask(backup.taskUUID);
    } catch (InterruptedException e) {
      LOG.info("Error while waiting for the backup task to get finished.");
    }
    backup.transitionState(BackupState.Stopped);
    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.withMessage("Successfully stopped the backup process.");
  }

  private static void waitForTask(UUID taskUUID) throws InterruptedException {
    int numRetries = 0;
    while (numRetries < maxRetryCount) {
      TaskInfo taskInfo = TaskInfo.get(taskUUID);
      if (TaskInfo.COMPLETED_STATES.contains(taskInfo.getTaskState())) {
        return;
      }
      Thread.sleep(1000);
      numRetries++;
    }
    throw new PlatformServiceException(
        BAD_REQUEST,
        "WaitFor task exceeded maxRetries! Task state is " + TaskInfo.get(taskUUID).getTaskState());
  }
}
