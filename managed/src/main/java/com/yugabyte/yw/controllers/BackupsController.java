// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackup;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.TaskInfoManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.ybc.YbcManager;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.DeleteBackupParams;
import com.yugabyte.yw.forms.DeleteBackupParams.DeleteBackupInfo;
import com.yugabyte.yw.forms.EditBackupParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.PlatformResults.YBPTasks;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.YbcThrottleParameters;
import com.yugabyte.yw.forms.filters.BackupApiFilter;
import com.yugabyte.yw.forms.paging.BackupPagedApiQuery;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CommonBackupInfo;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.filters.BackupFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.paging.BackupPagedApiResponse;
import com.yugabyte.yw.models.paging.BackupPagedQuery;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.regex.Pattern;
import javax.inject.Inject;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

@Api(value = "Backups", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class BackupsController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(BackupsController.class);
  private static final int maxRetryCount = 5;
  private static final String VALID_OWNER_REGEX = "^[\\pL_][\\pL\\pM_0-9]*$";

  private final Commissioner commissioner;
  private final CustomerConfigService customerConfigService;
  private final BackupUtil backupUtil;
  private final YbcManager ybcManager;

  @Inject
  public BackupsController(
      Commissioner commissioner,
      CustomerConfigService customerConfigService,
      BackupUtil backupUtil,
      YbcManager ybcManager) {
    this.commissioner = commissioner;
    this.customerConfigService = customerConfigService;
    this.backupUtil = backupUtil;
    this.ybcManager = ybcManager;
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
    Boolean isStorageLocMasked = isStorageLocationMasked(customerUUID);
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

  @ApiOperation(value = "Get Backup V2", response = Backup.class, nickname = "getBackupV2")
  public Result get(UUID customerUUID, UUID backupUUID) {
    Customer.getOrBadRequest(customerUUID);
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    Boolean isStorageLocMasked = isStorageLocationMasked(customerUUID);
    if (isStorageLocMasked) {
      BackupTableParams params = backup.getBackupInfo();
      String loc = params.storageLocation;
      if ((loc != null) && !loc.isEmpty()) {
        params.storageLocation = "**********";
      }
      backup.setBackupInfo(params);
    }
    return PlatformResults.withData(backup);
  }

  @ApiOperation(
      value = "List Backups (paginated) V2",
      response = BackupPagedApiResponse.class,
      nickname = "listBackupsV2")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageBackupsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.BackupPagedApiQuery",
          required = true))
  public Result pageBackupList(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    BackupPagedApiQuery apiQuery = parseJsonAndValidate(BackupPagedApiQuery.class);
    BackupApiFilter apiFilter = apiQuery.getFilter();
    BackupFilter filter = apiFilter.toFilter().toBuilder().customerUUID(customerUUID).build();
    BackupPagedQuery query = apiQuery.copyWithFilter(filter, BackupPagedQuery.class);

    BackupPagedApiResponse backups = Backup.pagedList(query);

    return PlatformResults.withData(backups);
  }

  @ApiOperation(
      value = "List Incremental backups",
      response = CommonBackupInfo.class,
      responseContainer = "List",
      nickname = "listIncrementalBackups")
  public Result listIncrementalBackups(UUID customerUUID, UUID baseBackupUUID) {
    Customer.getOrBadRequest(customerUUID);
    Backup.getOrBadRequest(customerUUID, baseBackupUUID);
    List<CommonBackupInfo> incrementalBackupChain =
        backupUtil.getIncrementalBackupList(baseBackupUUID, customerUUID);

    return PlatformResults.withData(incrementalBackupChain);
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

  @ApiOperation(value = "Create a backup", nickname = "createbackup", response = YBPTask.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Backup",
        value = "Backup data to be created",
        required = true,
        dataType = "com.yugabyte.yw.forms.BackupRequestParams",
        paramType = "body")
  })
  // Rename this to createBackup on completion
  public Result createBackupYb(UUID customerUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);

    BackupRequestParams taskParams = parseJsonAndValidate(BackupRequestParams.class);

    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(taskParams.universeUUID);
    taskParams.customerUUID = customerUUID;

    if (universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().backupInProgress) {
      throw new PlatformServiceException(
          CONFLICT,
          String.format(
              "Cannot run Backup task since the universe %s is currently in a locked state.",
              taskParams.universeUUID.toString()));
    }

    if ((universe.getLiveTServersInPrimaryCluster().size() < taskParams.parallelDBBackups)
        || taskParams.parallelDBBackups <= 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "invalid parallel backups value provided for universe %s", universe.universeUUID));
    }

    backupUtil.validateBackupRequest(taskParams.keyspaceTableList, universe, taskParams.backupType);

    if (taskParams.timeBeforeDelete != 0L && taskParams.expiryTimeUnit == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Please provide time unit for backup expiry");
    }

    if (taskParams.storageConfigUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Missing StorageConfig UUID: " + taskParams.storageConfigUUID);
    }
    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (!customerConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot create backup as config is queued for deletion.");
    }

    if (taskParams.baseBackupUUID != null) {
      Backup previousBackup =
          Backup.getLastSuccessfulBackupInChain(customerUUID, taskParams.baseBackupUUID);
      if (!universe.isYbcEnabled()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Incremental backup not allowed for non-YBC universes");
      } else if (previousBackup == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "No previous successful backup found, please trigger a new base backup.");
      }
      backupUtil.validateStorageConfigOnBackup(customerConfig, previousBackup);
    } else {
      backupUtil.validateStorageConfig(customerConfig);
    }

    UUID taskUUID = commissioner.submit(TaskType.CreateBackup, taskParams);
    LOG.info("Submitted task to universe {}, task uuid = {}.", universe.name, taskUUID);
    CustomerTask.create(
        customer,
        taskParams.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Create,
        universe.name);
    LOG.info("Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.name);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(taskParams), taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "Create Backup Schedule",
      response = Schedule.class,
      nickname = "createbackupSchedule")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "backup",
          value = "Parameters of the backup to be restored",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.BackupRequestParams",
          required = true))
  public Result createBackupSchedule(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    BackupRequestParams taskParams = parseJsonAndValidate(BackupRequestParams.class);
    if (taskParams.storageConfigUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Missing StorageConfig UUID: " + taskParams.storageConfigUUID);
    }
    if (taskParams.scheduleName == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Provide a name for the schedule");
    } else {
      if (Schedule.getScheduleByUniverseWithName(
              taskParams.scheduleName, taskParams.universeUUID, customerUUID)
          != null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Schedule with name " + taskParams.scheduleName + " already exist");
      }
    }
    if (taskParams.schedulingFrequency == 0L && taskParams.cronExpression == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Provide Cron Expression or Scheduling frequency");
    } else if (taskParams.schedulingFrequency != 0L && taskParams.cronExpression != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot provide both Cron Expression and Scheduling frequency");
    } else if (taskParams.schedulingFrequency != 0L) {
      BackupUtil.validateBackupFrequency(taskParams.schedulingFrequency);
      if (taskParams.frequencyTimeUnit == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Please provide time unit for scheduler frequency");
      }
    } else if (taskParams.cronExpression != null) {
      BackupUtil.validateBackupCronExpression(taskParams.cronExpression);
    }

    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (!customerConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot create backup as config is queued for deletion.");
    }
    backupUtil.validateStorageConfig(customerConfig);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(taskParams.universeUUID);
    taskParams.customerUUID = customerUUID;

    if (taskParams.keyspaceTableList != null) {
      for (BackupRequestParams.KeyspaceTable keyspaceTable : taskParams.keyspaceTableList) {
        if (keyspaceTable.tableUUIDList == null) {
          keyspaceTable.tableUUIDList = new ArrayList<UUID>();
        }
        backupUtil.validateTables(
            keyspaceTable.tableUUIDList, universe, keyspaceTable.keyspace, taskParams.backupType);
      }
    } else {
      backupUtil.validateTables(null, universe, null, taskParams.backupType);
    }
    if (taskParams.incrementalBackupFrequency != 0L) {
      if (taskParams.incrementalBackupFrequencyTimeUnit == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Please provide time unit for incremental backup frequency.");
      }
      if (taskParams.baseBackupUUID != null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot assign base backup while creating backup schedules.");
      }
      if (!universe.isYbcEnabled()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot create incremental backup schedules on non-ybc universes.");
      }
      // Validate Incremental backup schedule frequency
      long schedulingFrequency =
          (StringUtils.isEmpty(taskParams.cronExpression))
              ? taskParams.schedulingFrequency
              : BackupUtil.getCronExpressionTimeInterval(taskParams.cronExpression);
      BackupUtil.validateIncrementalScheduleFrequency(
          taskParams.incrementalBackupFrequency, schedulingFrequency);
    }
    Schedule schedule =
        Schedule.create(
            customerUUID,
            taskParams.universeUUID,
            taskParams,
            TaskType.CreateBackup,
            taskParams.schedulingFrequency,
            taskParams.cronExpression,
            taskParams.frequencyTimeUnit,
            taskParams.scheduleName);
    UUID scheduleUUID = schedule.getScheduleUUID();
    LOG.info(
        "Created backup schedule for customer {}, schedule uuid = {}.", customerUUID, scheduleUUID);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(taskParams));
    return PlatformResults.withData(schedule);
  }

  @ApiOperation(
      value = "Restore from a backup V2",
      response = YBPTask.class,
      responseContainer = "Restore",
      nickname = "restoreBackupV2")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "backup",
          value = "Parameters of the backup to be restored",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RestoreBackupParams",
          required = true))
  public Result restoreBackup(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    RestoreBackupParams taskParams = parseJsonAndValidate(RestoreBackupParams.class);
    taskParams.backupStorageInfoList.forEach(
        bSI -> {
          if (StringUtils.isNotBlank(bSI.newOwner)
              && !Pattern.matches(VALID_OWNER_REGEX, bSI.newOwner)) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Invalid owner rename during restore operation");
          }
        });

    taskParams.customerUUID = customerUUID;
    taskParams.prefixUUID = UUID.randomUUID();
    UUID universeUUID = taskParams.universeUUID;
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (CollectionUtils.isEmpty(taskParams.backupStorageInfoList)) {
      throw new PlatformServiceException(BAD_REQUEST, "Backup information not provided");
    }
    if (backupUtil.isYbcBackup(taskParams.backupStorageInfoList.get(0).storageLocation)) {
      taskParams.category = BackupCategory.YB_CONTROLLER;
    }
    backupUtil.validateRestoreOverwrites(
        taskParams.backupStorageInfoList, universe, taskParams.category);
    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (!customerConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot restore backup as config is queued for deletion.");
    }
    // Even though we check with default location below(line 393), this is needed to validate
    // regional locations, because their validity is not known to us when we send restore
    // request with a config.
    backupUtil.validateStorageConfig(customerConfig);
    CustomerConfigStorageData configData =
        (CustomerConfigStorageData) customerConfig.getDataObject();

    StorageUtil storageUtil = StorageUtil.getStorageUtil(customerConfig.name);
    Map<String, String> locationMap = new HashMap<>();
    for (BackupStorageInfo storageInfo : taskParams.backupStorageInfoList) {
      locationMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, storageInfo.storageLocation);
      storageUtil.validateStorageConfigOnLocations(configData, locationMap);
    }

    if (taskParams.category.equals(BackupCategory.YB_CONTROLLER) && !universe.isYbcEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot restore the ybc backup as ybc is not installed on the universe");
    }
    UUID taskUUID = commissioner.submit(TaskType.RestoreBackup, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Restore,
        universe.name);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RestoreBackup,
            Json.toJson(taskParams),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
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
    // Overriding the tableName in restore request as we don't support renaming of table.
    taskParams.setTableName(null);
    if (taskParams.storageLocation == null && taskParams.backupList == null) {
      String errMsg = "Storage Location is required";
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    if (taskParams.newOwner != null) {
      if (!Pattern.matches(VALID_OWNER_REGEX, taskParams.newOwner)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Invalid owner rename during restore operation");
      }
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
        "Submitted task to RESTORE table backup to {} with config {} from {}, task uuid = {}.",
        taskParams.getKeyspace(),
        storageConfig.configName,
        taskParams.storageLocation,
        taskUUID);
    if (taskParams.getKeyspace() != null) {
      // We cannot add long keySpace name in customer_task db table as in
      // the table schema we provide a 255 byte limit on target_name column of customer_task.
      // Currently, we set the limit of 500k on keySpace name size through
      // play.http.parser.maxMemoryBuffer.
      CustomerTask.create(
          customer,
          universeUUID,
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Restore,
          "keySpace");
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

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RestoreBackup,
            Json.toJson(formData),
            taskUUID);
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
          auditService()
              .createAuditEntryWithReqBody(
                  ctx(),
                  Audit.TargetType.Backup,
                  Objects.toString(backup.backupUUID, null),
                  Audit.ActionType.Delete,
                  Json.toJson(formData),
                  taskUUID);
        }
      }
    }
    return new YBPTasks(taskList).asResult();
  }

  @ApiOperation(
      value = "Delete backups V2",
      response = YBPTasks.class,
      nickname = "deleteBackupsV2")
  public Result deleteYb(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DeleteBackupParams deleteBackupParams = parseJsonAndValidate(DeleteBackupParams.class);
    List<YBPTask> taskList = new ArrayList<>();
    for (DeleteBackupInfo deleteBackupInfo : deleteBackupParams.deleteBackupInfos) {
      UUID backupUUID = deleteBackupInfo.backupUUID;
      Backup backup = Backup.maybeGet(customerUUID, backupUUID).orElse(null);
      if (backup == null) {
        LOG.error("Can not delete {} backup as it is not present in the database.", backupUUID);
      } else {
        if (Backup.IN_PROGRESS_STATES.contains(backup.state)) {
          LOG.error(
              "Backup {} is in the state {}. Deletion is not allowed", backupUUID, backup.state);
        } else {
          UUID storageConfigUUID = deleteBackupInfo.storageConfigUUID;
          if (storageConfigUUID == null) {
            // Pick default backup storage config to delete the backup if not provided.
            storageConfigUUID = backup.getBackupInfo().storageConfigUUID;
          }
          if (backup.isIncrementalBackup() && backup.state.equals(BackupState.Completed)) {
            // Currently, we don't allow users to delete successful standalone incremental backups.
            // They can only delete the full backup, along which all the incremental backups
            // will also be deleted.
            LOG.error("Cannot delete backup {} as it in {} state", backup.backupUUID, backup.state);
            continue;
          }
          BackupTableParams params = backup.getBackupInfo();
          params.storageConfigUUID = storageConfigUUID;
          backup.updateBackupInfo(params);
          DeleteBackupYb.Params taskParams = new DeleteBackupYb.Params();
          taskParams.customerUUID = customerUUID;
          taskParams.backupUUID = backupUUID;
          UUID taskUUID = commissioner.submit(TaskType.DeleteBackupYb, taskParams);
          LOG.info("Saved task uuid {} in customer tasks for backup {}.", taskUUID, backupUUID);
          String target =
              !StringUtils.isEmpty(backup.universeName)
                  ? backup.universeName
                  : String.format("univ-%s", backup.universeUUID.toString());
          CustomerTask.create(
              customer,
              backup.universeUUID,
              taskUUID,
              CustomerTask.TargetType.Backup,
              CustomerTask.TaskType.Delete,
              target);
          taskList.add(new YBPTask(taskUUID, taskParams.backupUUID));
          auditService()
              .createAuditEntryWithReqBody(
                  ctx(),
                  Audit.TargetType.Backup,
                  Objects.toString(backup.backupUUID, null),
                  Audit.ActionType.Delete,
                  request().body().asJson(),
                  taskUUID);
        }
      }
    }
    if (taskList.size() == 0) {
      auditService()
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.Backup,
              null,
              Audit.ActionType.Delete,
              request().body().asJson());
    }
    return new YBPTasks(taskList).asResult();
  }

  @ApiOperation(
      value = "Stop a backup",
      notes = "Stop an in-progress backup",
      nickname = "stopBackup",
      response = YBPSuccess.class)
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
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Backup,
            Objects.toString(backup.backupUUID, null),
            Audit.ActionType.Stop);
    return YBPSuccess.withMessage("Successfully stopped the backup process.");
  }

  @ApiOperation(
      value = "Edit a backup V2",
      notes = "Edit a backup",
      response = Backup.class,
      nickname = "editBackupV2")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "backup",
          value = "Parameters of the backup to be edited",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.EditBackupParams",
          required = true))
  public Result editBackup(UUID customerUUID, UUID backupUUID) {
    Customer.getOrBadRequest(customerUUID);
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    EditBackupParams taskParams = parseJsonAndValidate(EditBackupParams.class);
    if (taskParams.timeBeforeDeleteFromPresentInMillis <= 0L
        && taskParams.storageConfigUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Please provide either a positive expiry time or storage config to edit backup");
    } else if (Backup.IN_PROGRESS_STATES.contains(backup.state)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot edit a backup that is in progress state");
    } else if (taskParams.timeBeforeDeleteFromPresentInMillis > 0L
        && taskParams.expiryTimeUnit == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Please provide a time unit for backup expiry");
    } else if (!backup.backupUUID.equals(backup.baseBackupUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot edit an incremental backup");
    }
    if (taskParams.storageConfigUUID != null) {
      updateBackupStorageConfig(customerUUID, backupUUID, taskParams);
      LOG.info(
          "Updated Backup {} storage config UUID to {}", backupUUID, taskParams.storageConfigUUID);
    }
    if (taskParams.timeBeforeDeleteFromPresentInMillis > 0L) {
      backup.updateExpiryTime(taskParams.timeBeforeDeleteFromPresentInMillis);
      backup.updateExpiryTimeUnit(taskParams.expiryTimeUnit);
      LOG.info(
          "Updated Backup {} expiry time before delete to {} ms",
          backupUUID,
          taskParams.timeBeforeDeleteFromPresentInMillis);
    }
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Backup,
            Objects.toString(backup.backupUUID, null),
            Audit.ActionType.Edit,
            request().body().asJson());
    return PlatformResults.withData(backup);
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

  private Boolean isStorageLocationMasked(UUID customerUUID) {
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

    return isStorageLocMasked;
  }

  private void updateBackupStorageConfig(
      UUID customerUUID, UUID backupUUID, EditBackupParams taskParams) {
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    CustomerConfig existingConfig = CustomerConfig.get(customerUUID, backup.storageConfigUUID);
    if (existingConfig != null && existingConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Active storage config is already assigned to the backup");
    }
    CustomerConfig newConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (!newConfig.type.equals(ConfigType.STORAGE)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot assign " + newConfig.type + " type config in place of Storage Config");
    }

    if (!newConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot assign storage config which is not in Active state");
    }
    StorageConfigType backupConfigType = backup.getBackupInfo().storageConfigType;
    if (backupConfigType != null
        && !backupConfigType.equals(StorageConfigType.valueOf(newConfig.name))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot assign "
              + newConfig.name
              + " type config to the backup stored in "
              + backupConfigType);
    }
    backupUtil.validateStorageConfigOnBackup(newConfig, backup);
    backup.updateStorageConfigUUID(taskParams.storageConfigUUID);
  }

  @ApiOperation(
      value = "Set throttle params in YB-Controller",
      nickname = "setThrottleParams",
      response = YBPSuccess.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "throttleParams",
          value = "Parameters for YB-Controller throttling",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.YbcThrottleParameters",
          required = true))
  public Result setThrottleParams(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID.
    Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID.
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (universe.universeIsLocked() || universe.getUniverseDetails().backupInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot set throttle params, universe task in progress.");
    }
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot set throttle params, universe is paused.");
    }
    if (!universe.isYbcEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot set throttle params, universe does not have YB-Controller setup.");
    }
    YbcThrottleParameters throttleParams = parseJsonAndValidate(YbcThrottleParameters.class);
    try {
      ybcManager.setThrottleParams(universeUUID, throttleParams);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Got error setting throttle params for universe {}, error: {}",
              universeUUID.toString(),
              e.getMessage()));
    }
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            Objects.toString(universeUUID, null),
            Audit.ActionType.SetThrottleParams,
            request().body().asJson());
    return YBPSuccess.withMessage("Set throttle params for universe " + universeUUID.toString());
  }

  @ApiOperation(
      value = "Get throttle params from YB-Controller",
      nickname = "getThrottleparams",
      response = Map.class)
  public Result getThrottleParams(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID
    Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (!universe.isYbcEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot get throttle params, universe does not have YB-Controller setup.");
    }
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot get throttle params, universe is paused.");
    }
    try {
      Map<String, String> throttleParams = ybcManager.getThrottleParams(universeUUID);
      return PlatformResults.withData(throttleParams);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Got error getting throttle params for universe {}, error: {}",
              universeUUID.toString(),
              e.getMessage()));
    }
  }
}
