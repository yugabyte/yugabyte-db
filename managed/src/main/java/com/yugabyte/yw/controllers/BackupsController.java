// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackup;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.TaskInfoManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
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
import com.yugabyte.yw.forms.RestorePreflightParams;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.YbcThrottleParameters;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.forms.filters.BackupApiFilter;
import com.yugabyte.yw.forms.filters.RestoreApiFilter;
import com.yugabyte.yw.forms.paging.BackupPagedApiQuery;
import com.yugabyte.yw.forms.paging.RestorePagedApiQuery;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import com.yugabyte.yw.models.CommonBackupInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.filters.BackupFilter;
import com.yugabyte.yw.models.filters.RestoreFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.paging.BackupPagedApiResponse;
import com.yugabyte.yw.models.paging.BackupPagedQuery;
import com.yugabyte.yw.models.paging.RestorePagedApiResponse;
import com.yugabyte.yw.models.paging.RestorePagedQuery;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Inject;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes;
import org.yb.CommonTypes.TableType;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(value = "Backups", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class BackupsController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(BackupsController.class);
  private static final int maxRetryCount = 5;
  private static final String VALID_OWNER_REGEX = "^[\\pL_][\\pL\\pM_0-9]*$";

  private final Commissioner commissioner;
  private final CustomerConfigService customerConfigService;
  private final BackupHelper backupHelper;
  private final YbcManager ybcManager;
  private final StorageUtilFactory storageUtilFactory;

  @Inject
  public BackupsController(
      Commissioner commissioner,
      CustomerConfigService customerConfigService,
      BackupHelper backupHelper,
      YbcManager ybcManager,
      StorageUtilFactory storageUtilFactory) {
    this.commissioner = commissioner;
    this.customerConfigService = customerConfigService;
    this.backupHelper = backupHelper;
    this.ybcManager = ybcManager;
    this.storageUtilFactory = storageUtilFactory;
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
  public Result pageBackupList(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    BackupPagedApiQuery apiQuery = parseJsonAndValidate(request, BackupPagedApiQuery.class);
    BackupApiFilter apiFilter = apiQuery.getFilter();
    BackupFilter filter = apiFilter.toFilter().toBuilder().customerUUID(customerUUID).build();
    BackupPagedQuery query = apiQuery.copyWithFilter(filter, BackupPagedQuery.class);

    BackupPagedApiResponse backups = Backup.pagedList(query);

    return PlatformResults.withData(backups);
  }

  @ApiOperation(
      value = "List Backup Restores (paginated)",
      response = RestorePagedApiResponse.class,
      nickname = "listBackupRestoresV2")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageRestoresRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.RestorePagedApiQuery",
          required = true))
  public Result pageRestoreList(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    RestorePagedApiQuery apiQuery = parseJsonAndValidate(request, RestorePagedApiQuery.class);
    RestoreApiFilter apiFilter = apiQuery.getFilter();
    RestoreFilter filter = apiFilter.toFilter().toBuilder().customerUUID(customerUUID).build();
    RestorePagedQuery query = apiQuery.copyWithFilter(filter, RestorePagedQuery.class);

    RestorePagedApiResponse restores = Restore.pagedList(query);

    return PlatformResults.withData(restores);
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
        BackupUtil.getIncrementalBackupList(baseBackupUUID, customerUUID);

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
  public Result createBackupYb(UUID customerUUID, Http.Request request) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);

    BackupRequestParams taskParams = parseJsonAndValidate(request, BackupRequestParams.class);

    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent primaryClusterUserIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    taskParams.customerUUID = customerUUID;

    if (universe
        .getConfig()
        .getOrDefault(Universe.TAKE_BACKUPS, "true")
        .equalsIgnoreCase("false")) {
      throw new PlatformServiceException(BAD_REQUEST, "Taking backups on the universe is disabled");
    }

    if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          CONFLICT,
          String.format(
              "Cannot run Backup task since the universe %s is currently in a locked state.",
              taskParams.getUniverseUUID().toString()));
    }

    if ((universe.getLiveTServersInPrimaryCluster().size() < taskParams.parallelDBBackups)
        || taskParams.parallelDBBackups <= 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "invalid parallel backups value provided for universe %s",
              universe.getUniverseUUID()));
    }

    backupHelper.validateBackupRequest(
        taskParams.keyspaceTableList, universe, taskParams.backupType);

    if (taskParams.timeBeforeDelete != 0L && taskParams.expiryTimeUnit == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Please provide time unit for backup expiry");
    }

    if (taskParams.backupType != null) {
      if (taskParams.backupType.equals(TableType.PGSQL_TABLE_TYPE)
          && !primaryClusterUserIntent.enableYSQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YSQL tables if API is disabled");
      } else if (taskParams.backupType.equals(TableType.YQL_TABLE_TYPE)
          && !primaryClusterUserIntent.enableYCQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YCQL tables if API is disabled");
      }
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
      backupHelper.validateStorageConfigOnBackup(customerConfig, previousBackup);
    } else {
      backupHelper.validateStorageConfig(customerConfig);
    }

    UUID taskUUID = commissioner.submit(TaskType.CreateBackup, taskParams);
    LOG.info("Submitted task to universe {}, task uuid = {}.", universe.getName(), taskUUID);
    CustomerTask.create(
        customer,
        taskParams.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Create,
        universe.getName());
    LOG.info("Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.getName());
    auditService().createAuditEntry(request, Json.toJson(taskParams), taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "Create Backup Schedule Async",
      response = YBPTask.class,
      nickname = "createBackupScheduleAsync")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "backup",
          value = "Parameters of the backup to be restored",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.BackupRequestParams",
          required = true))
  public Result createBackupScheduleAsync(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    BackupRequestParams taskParams = parseJsonAndValidate(request, BackupRequestParams.class);
    validateScheduleTaskParams(taskParams, customerUUID);

    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());

    UUID taskUUID = commissioner.submit(TaskType.CreateBackupSchedule, taskParams);
    LOG.info("Submitted task to universe {}, task uuid = {}.", universe.getName(), taskUUID);
    CustomerTask.create(
        customer,
        taskParams.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Schedule,
        CustomerTask.TaskType.Create,
        universe.getName());
    LOG.info("Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.getName());
    auditService().createAuditEntry(request, Json.toJson(taskParams), taskUUID);
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
  @Deprecated
  public Result createBackupSchedule(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    BackupRequestParams taskParams = parseJsonAndValidate(request, BackupRequestParams.class);
    validateScheduleTaskParams(taskParams, customerUUID);

    Schedule schedule =
        Schedule.create(
            customerUUID,
            taskParams.getUniverseUUID(),
            taskParams,
            TaskType.CreateBackup,
            taskParams.schedulingFrequency,
            taskParams.cronExpression,
            taskParams.frequencyTimeUnit,
            taskParams.scheduleName);
    UUID scheduleUUID = schedule.getScheduleUUID();
    LOG.info(
        "Created backup schedule for customer {}, schedule uuid = {}.", customerUUID, scheduleUUID);
    auditService().createAuditEntryWithReqBody(request);
    return PlatformResults.withData(schedule);
  }

  private void validateScheduleTaskParams(BackupRequestParams taskParams, UUID customerUUID) {
    if (taskParams.storageConfigUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Missing StorageConfig UUID: " + taskParams.storageConfigUUID);
    }
    if (taskParams.scheduleName == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Provide a name for the schedule");
    } else {
      if (Schedule.getScheduleByUniverseWithName(
              taskParams.scheduleName, taskParams.getUniverseUUID(), customerUUID)
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
    backupHelper.validateStorageConfig(customerConfig);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent primaryClusterUserIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    taskParams.customerUUID = customerUUID;

    if (taskParams.backupType != null) {
      if (taskParams.backupType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)
          && !primaryClusterUserIntent.enableYSQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YSQL tables if API is disabled");
      } else if (taskParams.backupType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)
          && !primaryClusterUserIntent.enableYCQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YCQL tables if API is disabled");
      }
    }

    if (taskParams.keyspaceTableList != null) {
      for (BackupRequestParams.KeyspaceTable keyspaceTable : taskParams.keyspaceTableList) {
        if (keyspaceTable.tableUUIDList == null) {
          keyspaceTable.tableUUIDList = new ArrayList<UUID>();
        }
        backupHelper.validateTables(
            keyspaceTable.tableUUIDList, universe, keyspaceTable.keyspace, taskParams.backupType);
      }
    } else {
      backupHelper.validateTables(null, universe, null, taskParams.backupType);
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
      backupHelper.validateIncrementalScheduleFrequency(
          taskParams.incrementalBackupFrequency, schedulingFrequency, universe);
    }
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
  public Result restoreBackup(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    RestoreBackupParams taskParams = parseJsonAndValidate(request, RestoreBackupParams.class);
    UUID universeUUID = taskParams.getUniverseUUID();
    Universe universe = Universe.getOrBadRequest(universeUUID);
    UniverseDefinitionTaskParams.UserIntent primaryClusterUserIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;

    taskParams.backupStorageInfoList.forEach(
        bSI -> {
          if (StringUtils.isNotBlank(bSI.newOwner)
              && !Pattern.matches(VALID_OWNER_REGEX, bSI.newOwner)) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Invalid owner rename during restore operation");
          }
          if (bSI.backupType != null) {
            if (bSI.backupType.equals(TableType.PGSQL_TABLE_TYPE)
                && !primaryClusterUserIntent.enableYSQL) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Cannot take backups on YSQL tables if API is disabled");
            } else if (bSI.backupType.equals(TableType.YQL_TABLE_TYPE)
                && !primaryClusterUserIntent.enableYCQL) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Cannot take backups on YCQL tables if API is disabled");
            }
          }
        });

    taskParams.customerUUID = customerUUID;
    taskParams.prefixUUID = UUID.randomUUID();
    if (CollectionUtils.isEmpty(taskParams.backupStorageInfoList)) {
      throw new PlatformServiceException(BAD_REQUEST, "Backup information not provided");
    }
    backupHelper.validateRestoreOverwrites(
        taskParams.backupStorageInfoList, universe, taskParams.category);
    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (!customerConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot restore backup as config is queued for deletion.");
    }
    // Even though we check with default location below, this is needed to validate
    // regional locations, because their validity is not known to us when we send restore
    // request with a config.
    backupHelper.validateStorageConfig(customerConfig);
    CustomerConfigStorageData configData =
        (CustomerConfigStorageData) customerConfig.getDataObject();

    storageUtilFactory
        .getStorageUtil(customerConfig.getName())
        .validateStorageConfigOnLocationsList(
            configData,
            taskParams.backupStorageInfoList.parallelStream()
                .map(bSI -> bSI.storageLocation)
                .collect(Collectors.toSet()));

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
        universe.getName());
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RestoreBackup,
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
  public Result restore(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    Form<BackupTableParams> formData =
        formFactory.getFormDataOrBadRequest(request, BackupTableParams.class);

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

    taskParams.setUniverseUUID(universeUUID);
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
        subParams.setUniverseUUID(universeUUID);
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
        storageConfig.getConfigName(),
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
          universe.getName());
      if (taskParams.backupList != null) {
        LOG.info(
            "Saved task uuid {} in customer tasks table for universe backup {}",
            taskUUID,
            universe.getName());
      } else {
        LOG.info(
            "Saved task uuid {} in customer tasks table for restore identical "
                + "keyspace & tables in universe {}",
            taskUUID,
            universe.getName());
      }
    }
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RestoreBackup,
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(value = "Delete backups", response = YBPTasks.class, nickname = "deleteBackups")
  public Result delete(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // TODO(API): Let's get rid of raw Json.
    // Create DeleteBackupReq in form package and bind to that
    ObjectNode formData = (ObjectNode) request.body().asJson();
    List<YBPTask> taskList = new ArrayList<>();
    for (JsonNode backupUUID : formData.get("backupUUID")) {
      UUID uuid = UUID.fromString(backupUUID.asText());
      Backup backup = Backup.get(customerUUID, uuid);
      if (backup == null) {
        LOG.info(
            "Can not delete {} backup as it is not present in the database.", backupUUID.asText());
      } else {
        if (backup.getState() != Backup.BackupState.Completed
            && backup.getState() != Backup.BackupState.Failed) {
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
              backup.getBackupInfo().getUniverseUUID(),
              taskUUID,
              CustomerTask.TargetType.Backup,
              CustomerTask.TaskType.Delete,
              "Backup");
          taskList.add(new YBPTask(taskUUID, taskParams.backupUUID));
          auditService()
              .createAuditEntryWithReqBody(
                  request,
                  Audit.TargetType.Backup,
                  Objects.toString(backup.getBackupUUID(), null),
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
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "deleteBackup",
          value = "Parameters of the backup to be deleted",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.DeleteBackupParams",
          required = true))
  public Result deleteYb(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DeleteBackupParams deleteBackupParams = parseJsonAndValidate(request, DeleteBackupParams.class);
    List<YBPTask> taskList = new ArrayList<>();
    for (DeleteBackupInfo deleteBackupInfo : deleteBackupParams.deleteBackupInfos) {
      UUID backupUUID = deleteBackupInfo.backupUUID;
      Backup backup = Backup.maybeGet(customerUUID, backupUUID).orElse(null);
      if (backup == null) {
        LOG.error("Can not delete {} backup as it is not present in the database.", backupUUID);
      } else {
        if (Backup.IN_PROGRESS_STATES.contains(backup.getState())) {
          LOG.error(
              "Backup {} is in the state {}. Deletion is not allowed",
              backupUUID,
              backup.getState());
        } else {
          UUID storageConfigUUID = deleteBackupInfo.storageConfigUUID;
          if (storageConfigUUID == null) {
            // Pick default backup storage config to delete the backup if not provided.
            storageConfigUUID = backup.getBackupInfo().storageConfigUUID;
          }
          if (backup.isIncrementalBackup() && backup.getState().equals(BackupState.Completed)) {
            // Currently, we don't allow users to delete successful standalone incremental backups.
            // They can only delete the full backup, along which all the incremental backups
            // will also be deleted.
            LOG.error(
                "Cannot delete backup {} as it in {} state",
                backup.getBackupUUID(),
                backup.getState());
            continue;
          }
          BackupTableParams params = backup.getBackupInfo();
          params.storageConfigUUID = storageConfigUUID;
          backup.updateBackupInfo(params);
          DeleteBackupYb.Params taskParams = new DeleteBackupYb.Params();
          taskParams.customerUUID = customerUUID;
          taskParams.backupUUID = backupUUID;
          taskParams.deleteForcefully = deleteBackupParams.deleteForcefully;
          UUID taskUUID = commissioner.submit(TaskType.DeleteBackupYb, taskParams);
          LOG.info("Saved task uuid {} in customer tasks for backup {}.", taskUUID, backupUUID);
          String target =
              !StringUtils.isEmpty(backup.getUniverseName())
                  ? backup.getUniverseName()
                  : String.format("univ-%s", backup.getUniverseUUID().toString());
          CustomerTask.create(
              customer,
              backup.getUniverseUUID(),
              taskUUID,
              CustomerTask.TargetType.Backup,
              CustomerTask.TaskType.Delete,
              target);
          taskList.add(new YBPTask(taskUUID, taskParams.backupUUID));
          auditService()
              .createAuditEntryWithReqBody(
                  request,
                  Audit.TargetType.Backup,
                  Objects.toString(backup.getBackupUUID(), null),
                  Audit.ActionType.Delete,
                  taskUUID);
        }
      }
    }
    if (taskList.size() == 0) {
      auditService()
          .createAuditEntryWithReqBody(
              request, Audit.TargetType.Backup, null, Audit.ActionType.Delete);
    }
    return new YBPTasks(taskList).asResult();
  }

  @ApiOperation(
      value = "Stop a backup",
      notes = "Stop an in-progress backup",
      nickname = "stopBackup",
      response = YBPSuccess.class)
  public Result stop(UUID customerUUID, UUID backupUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Process process = Util.getProcessOrBadRequest(backupUUID);
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    if (backup.getState() != Backup.BackupState.InProgress) {
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
      waitForTask(backup.getTaskUUID());
    } catch (InterruptedException e) {
      LOG.info("Error while waiting for the backup task to get finished.");
    }
    backup.transitionState(BackupState.Stopped);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Backup,
            Objects.toString(backup.getBackupUUID(), null),
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
  public Result editBackup(UUID customerUUID, UUID backupUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    EditBackupParams taskParams = parseJsonAndValidate(request, EditBackupParams.class);
    if (taskParams.timeBeforeDeleteFromPresentInMillis < 0L
        && taskParams.storageConfigUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Please provide either a non negative expiry time or storage config to edit backup");
    } else if (Backup.IN_PROGRESS_STATES.contains(backup.getState())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot edit a backup that is in progress state");
    } else if (taskParams.timeBeforeDeleteFromPresentInMillis > 0L
        && taskParams.expiryTimeUnit == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Please provide a time unit for backup expiry");
    } else if (!backup.getBackupUUID().equals(backup.getBaseBackupUUID())) {
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
    } else if (taskParams.timeBeforeDeleteFromPresentInMillis == 0L) {
      backup.unsetExpiry();
      LOG.info("Updated Backup {} expiry to never expire", backupUUID);
    }
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Backup,
            Objects.toString(backup.getBackupUUID(), null),
            Audit.ActionType.Edit);
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
      UserWithFeatures user = RequestContext.get(TokenAuthenticator.USER);
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
    CustomerConfig existingConfig = CustomerConfig.get(customerUUID, backup.getStorageConfigUUID());
    if (existingConfig != null && existingConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Active storage config is already assigned to the backup");
    }
    CustomerConfig newConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (!newConfig.getType().equals(ConfigType.STORAGE)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot assign " + newConfig.getType() + " type config in place of Storage Config");
    }

    if (!newConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot assign storage config which is not in Active state");
    }
    StorageConfigType backupConfigType = backup.getBackupInfo().storageConfigType;
    if (backupConfigType != null
        && !backupConfigType.equals(StorageConfigType.valueOf(newConfig.getName()))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot assign "
              + newConfig.getName()
              + " type config to the backup stored in "
              + backupConfigType);
    }
    backupHelper.validateStorageConfigOnBackup(newConfig, backup);
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
  public Result setThrottleParams(UUID customerUUID, UUID universeUUID, Http.Request request) {
    // Validate customer UUID.
    Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID.
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (universe.universeIsLocked()) {
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
    YbcThrottleParameters throttleParams =
        parseJsonAndValidate(request, YbcThrottleParameters.class);
    try {
      ybcManager.setThrottleParams(universeUUID, throttleParams);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Got error setting throttle params for universe %s, error: %s",
              universeUUID.toString(), e.getMessage()));
    }
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            Objects.toString(universeUUID, null),
            Audit.ActionType.SetThrottleParams);
    return YBPSuccess.withMessage("Set throttle params for universe " + universeUUID.toString());
  }

  @ApiOperation(
      value = "Get throttle params from YB-Controller",
      nickname = "getThrottleParams",
      response = YbcThrottleParametersResponse.class)
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
      YbcThrottleParametersResponse throttleParams = ybcManager.getThrottleParams(universeUUID);
      return PlatformResults.withData(throttleParams);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Got error getting throttle params for universe %s, error: %s",
              universeUUID.toString(), e.getMessage()));
    }
  }

  @ApiOperation(
      value = "Restore preflight checks",
      nickname = "restorePreflight",
      response = RestorePreflightResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "restorePreflightParams",
          value = "Parameters fr restore preflight check",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RestorePreflightParams",
          required = true))
  public Result restorePreflight(UUID customerUUID, Http.Request request) {
    // Validate customer
    Customer.getOrBadRequest(customerUUID);

    RestorePreflightParams preflightParams =
        parseJsonAndValidate(request, RestorePreflightParams.class);
    try {
      RestorePreflightResponse restorePreflightResponse =
          backupHelper.generateRestorePreflightAPIResponse(preflightParams, customerUUID);
      return PlatformResults.withData(restorePreflightResponse);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Running restore preflight failed for universe %s failed with error: %s",
              preflightParams.getUniverseUUID().toString(), e.getMessage()));
    }
  }
}
