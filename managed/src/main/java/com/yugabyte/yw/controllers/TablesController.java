// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.Common.CloudType.aws;
import static com.yugabyte.yw.common.Util.getUUIDRepresentation;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.CreateTablespaceParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import com.yugabyte.yw.forms.TableInfoForm;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponse;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.RelationType;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Http.Request;
import play.mvc.Result;

@Api(
    value = "Table management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class TablesController extends AuthenticatedController {

  private static final Logger LOG = LoggerFactory.getLogger(TablesController.class);

  Commissioner commissioner;

  private final YBClientService ybService;

  private final CustomerConfigService customerConfigService;

  private final UniverseTableHandler tableHandler;

  @Inject
  public TablesController(
      Commissioner commissioner,
      YBClientService service,
      CustomerConfigService customerConfigService,
      UniverseTableHandler tableHandler) {
    this.commissioner = commissioner;
    this.ybService = service;
    this.customerConfigService = customerConfigService;
    this.tableHandler = tableHandler;
  }

  @ApiOperation(
      value = "YbaApi Internal. Create a YugabyteDB table",
      response = YBPTask.class,
      nickname = "createTable")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Table",
        value = "Table definition to be created",
        required = true,
        dataType = "com.yugabyte.yw.forms.TableDefinitionTaskParams",
        paramType = "body")
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result create(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Form<TableDefinitionTaskParams> formData =
        formFactory.getFormDataOrBadRequest(request, TableDefinitionTaskParams.class);
    TableDefinitionTaskParams taskParams = formData.get();
    UUID taskUUID = tableHandler.create(customerUUID, universeUUID, taskParams);
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Table, null, Audit.ActionType.Create, taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "YbaApi Internal. Alter a YugabyteDB table",
      nickname = "alterTable",
      response = Object.class,
      responseContainer = "Map")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result alter(UUID cUUID, UUID uniUUID, UUID tableUUID, Request request) {
    return TODO(request);
  }

  @ApiOperation(
      value = "YbaApi Internal. Drop a YugabyteDB table",
      nickname = "dropTable",
      response = YBPTask.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result drop(UUID customerUUID, UUID universeUUID, UUID tableUUID, Http.Request request) {
    UUID taskUUID = tableHandler.drop(customerUUID, universeUUID, tableUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Table, tableUUID.toString(), Audit.ActionType.Drop, taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "YbaApi Internal. UI_ONLY",
      response = Object.class,
      responseContainer = "Map",
      hidden = true)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.2.0.0")
  @AuthzPath
  public Result getColumnTypes() {
    ColumnDetails.YQLDataType[] dataTypes = ColumnDetails.YQLDataType.values();
    ObjectNode result = Json.newObject();
    ArrayNode primitives = Json.newArray();
    ArrayNode collections = Json.newArray();
    for (ColumnDetails.YQLDataType dataType : dataTypes) {
      if (dataType.isCollection()) {
        collections.add(dataType.name());
      } else {
        primitives.add(dataType.name());
      }
    }
    result.put("primitives", primitives);
    result.put("collections", collections);
    return PlatformResults.withRawData(result);
  }

  @ApiOperation(
      value = "YbaApi Internal. List column types",
      notes = "Get a list of all defined column types.",
      response = ColumnDetails.YQLDataType.class,
      responseContainer = "List")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.8.0.0")
  @AuthzPath
  public Result getYQLDataTypes() {
    return PlatformResults.withData(ColumnDetails.YQLDataType.values());
  }

  @ApiOperation(
      value = "YbaApi Internal. List all tables",
      nickname = "getAllTables",
      notes = "Get a list of all tables in the specified universe",
      response = TableInfoForm.TableInfoResp.class,
      responseContainer = "List")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.8.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result listTables(
      UUID customerUUID,
      UUID universeUUID,
      boolean includeParentTableInfo,
      boolean excludeColocatedTables,
      boolean includeColocatedParentTables,
      boolean xClusterSupportedOnly) {
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customerUUID,
            universeUUID,
            includeParentTableInfo,
            excludeColocatedTables,
            includeColocatedParentTables,
            xClusterSupportedOnly);
    return PlatformResults.withData(resp);
  }

  @ApiOperation(
      value = "YbaApi Internal. List all namespaces",
      nickname = "getAllNamespaces",
      notes = "Get a list of all namespaces in the specified universe",
      response = TableInfoForm.NamespaceInfoResp.class,
      responseContainer = "List")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result listNamespaces(
      UUID customerUUID, UUID universeUUID, boolean includeSystemNamespaces) {
    List<TableInfoForm.NamespaceInfoResp> response =
        tableHandler.listNamespaces(customerUUID, universeUUID, includeSystemNamespaces);
    return PlatformResults.withData(response);
  }

  /**
   * This API will describe a single table.
   *
   * @param customerUUID UUID of the customer owning the table
   * @param universeUUID UUID of the universe in which the table resides
   * @param tableUUID UUID or ID of the table to describe
   * @return json-serialized description of the table
   */
  @ApiOperation(
      value = "YbaApi Internal. Describe a table",
      nickname = "describeTable",
      response = TableDefinitionTaskParams.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result describe(UUID customerUUID, UUID universeUUID, String tableUUID) {
    UUID tableUuid =
        tableUUID.contains("-") ? UUID.fromString(tableUUID) : getUUIDRepresentation(tableUUID);
    return PlatformResults.withData(tableHandler.describe(customerUUID, universeUUID, tableUuid));
  }

  @ApiOperation(
      value =
          "Deprecated since YBA version 2.20.0.0 (Use BackupsController). "
              + "Create a multi-table backup",
      tags = {"Backups", "Table management"},
      nickname = "createMultiTableBackup",
      response = YBPTask.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
  @ApiResponses(
      @ApiResponse(
          code = 200,
          message = "If requested schedule backup.",
          response = Schedule.class))
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Table backup",
        value = "Table backup data to be created",
        required = true,
        dataType = "com.yugabyte.yw.forms.MultiTableBackupRequestParams",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @Deprecated
  public Result createMultiTableBackup(UUID customerUUID, UUID universeUUID, Http.Request request) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    Form<MultiTableBackup.Params> formData =
        formFactory.getFormDataOrBadRequest(request, MultiTableBackup.Params.class);

    MultiTableBackup.Params taskParams = formData.get();
    if (taskParams.storageConfigUUID == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Missing StorageConfig UUID: " + null);
    }
    customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot run Backup task since the universe %s is currently in a locked state.",
              universeUUID.toString()));
    }

    taskParams.setUniverseUUID(universeUUID);
    taskParams.customerUUID = customerUUID;

    validateTables(
        taskParams.tableUUIDList, universe, taskParams.getKeyspace(), taskParams.backupType);

    if (taskParams.schedulingFrequency != 0L || taskParams.cronExpression != null) {
      Schedule schedule =
          Schedule.create(
              customerUUID,
              taskParams,
              TaskType.MultiTableBackup,
              taskParams.schedulingFrequency,
              taskParams.cronExpression);
      UUID scheduleUUID = schedule.getScheduleUUID();
      LOG.info(
          "Submitted universe backup to be scheduled {}, schedule uuid = {}.",
          universeUUID,
          scheduleUUID);
      auditService()
          .createAuditEntryWithReqBody(
              request,
              Audit.TargetType.Universe,
              universeUUID.toString(),
              Audit.ActionType.CreateMultiTableBackup);
      return PlatformResults.withData(schedule);
    } else {
      UUID taskUUID = commissioner.submit(TaskType.MultiTableBackup, taskParams);
      LOG.info("Submitted task to universe {}, task uuid = {}.", universe.getName(), taskUUID);
      CustomerTask.create(
          customer,
          taskParams.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Create,
          universe.getName());
      LOG.info(
          "Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.getName());
      auditService()
          .createAuditEntryWithReqBody(
              request,
              Audit.TargetType.Universe,
              universeUUID.toString(),
              Audit.ActionType.CreateMultiTableBackup,
              taskUUID);
      return new YBPTask(taskUUID).asResult();
    }
  }

  @ApiOperation(
      value =
          "Deprecated since YBA version 2.20.0.0 (Use BackupsController). "
              + "Create a single-table backup",
      nickname = "createSingleTableBackup",
      response = YBPTask.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Backup",
        value = "Backup data to be created",
        required = true,
        dataType = "com.yugabyte.yw.forms.BackupTableParams",
        paramType = "body")
  })
  @YbaApi(visibility = YbaApi.YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @Deprecated
  // Remove this too.
  public Result createBackup(
      UUID customerUUID, UUID universeUUID, UUID tableUUID, Http.Request request) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    Form<BackupTableParams> formData =
        formFactory.getFormDataOrBadRequest(request, BackupTableParams.class);
    BackupTableParams taskParams = formData.get();

    validateTables(
        Collections.singletonList(tableUUID),
        universe,
        taskParams.getKeyspace(),
        taskParams.backupType);

    customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot run Backup task since the universe %s is currently in a locked state.",
              universeUUID.toString()));
    }

    taskParams.setUniverseUUID(universeUUID);
    taskParams.tableUUID = tableUUID;
    taskParams.customerUuid = customerUUID;

    if (taskParams.schedulingFrequency != 0L || taskParams.cronExpression != null) {
      Schedule schedule =
          Schedule.create(
              customerUUID,
              taskParams,
              TaskType.BackupUniverse,
              taskParams.schedulingFrequency,
              taskParams.cronExpression);
      UUID scheduleUUID = schedule.getScheduleUUID();
      LOG.info(
          "Submitted backup to be scheduled {}:{}, schedule uuid = {}.",
          tableUUID,
          CommonUtils.logTableName(taskParams.getTableName()),
          scheduleUUID);
      auditService()
          .createAuditEntryWithReqBody(
              request,
              Audit.TargetType.Table,
              tableUUID.toString(),
              Audit.ActionType.CreateSingleTableBackup);
      return PlatformResults.withData(schedule);
    } else {
      UUID taskUUID = commissioner.submit(TaskType.BackupUniverse, taskParams);
      LOG.info(
          "Submitted task to backup table {}:{}, task uuid = {}.",
          tableUUID,
          CommonUtils.logTableName(taskParams.getTableName()),
          taskUUID);
      CustomerTask.create(
          customer,
          taskParams.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Create,
          taskParams.getTableName());
      LOG.info(
          "Saved task uuid {} in customer tasks table for table {}:{}.{}",
          taskUUID,
          tableUUID,
          taskParams.getTableNames(),
          CommonUtils.logTableName(taskParams.getTableName()));
      auditService()
          .createAuditEntryWithReqBody(
              request,
              Audit.TargetType.Table,
              tableUUID.toString(),
              Audit.ActionType.CreateSingleTableBackup,
              taskUUID);
      return new YBPTask(taskUUID).asResult();
    }
  }

  /**
   * This API will allow a customer to bulk import data into a table.
   *
   * @param customerUUID UUID of the customer owning the table.
   * @param universeUUID UUID of the universe in which the table resides.
   * @param tableUUID UUID of the table to describe.
   */
  @ApiOperation(
      value = "YbaApi Internal. Bulk import data",
      nickname = "bulkImportData",
      notes = "Bulk import data into the specified table. This is currently AWS-only.",
      response = YBPTask.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.2.0.0")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Bulk import",
        value = "Bulk data to be imported",
        required = true,
        dataType = "com.yugabyte.yw.forms.BulkImportParams",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result bulkImport(
      UUID customerUUID, UUID universeUUID, UUID tableUUID, Http.Request request) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    validateTables(Collections.singletonList(tableUUID), universe);

    // TODO: undo hardcode to AWS (required right now due to using EMR).
    Common.CloudType cloudType =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    if (cloudType != aws) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Bulk Import is currently only supported for AWS.");
    }

    Provider.getOrBadRequest(
        customerUUID,
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));

    // Get form data and validate it.
    Form<BulkImportParams> formData =
        formFactory.getFormDataOrBadRequest(request, BulkImportParams.class);
    BulkImportParams taskParams = formData.get();
    if (taskParams.s3Bucket == null || !taskParams.s3Bucket.startsWith("s3://")) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid S3 Bucket provided: " + taskParams.s3Bucket);
    }
    taskParams.setUniverseUUID(universeUUID);

    UUID taskUUID = commissioner.submit(TaskType.ImportIntoTable, taskParams);
    LOG.info(
        "Submitted import into table for {}:{}, task uuid = {}.",
        tableUUID,
        CommonUtils.logTableName(taskParams.getTableName()),
        taskUUID);

    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Table,
        CustomerTask.TaskType.BulkImportData,
        taskParams.getTableName());
    LOG.info(
        "Saved task uuid {} in customer tasks table for table {}:{}.{}",
        taskUUID,
        tableUUID,
        CommonUtils.logTableName(taskParams.getTableName()),
        CommonUtils.logTableName(taskParams.getTableName()));

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Table,
            tableUUID.toString(),
            Audit.ActionType.BulkImport,
            taskUUID);
    return new YBPTask(taskUUID, tableUUID).asResult();
  }

  @VisibleForTesting
  void validateTables(
      List<UUID> tableUuids, Universe universe, String keyspace, TableType tableType) {

    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    if (keyspace != null && tableUuids.isEmpty()) {
      tableInfoList =
          tableInfoList.parallelStream()
              .filter(tableInfo -> keyspace.equals(tableInfo.getNamespace().getName()))
              .filter(tableInfo -> tableType.equals(tableInfo.getTableType()))
              .collect(Collectors.toList());
      if (tableInfoList.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot initiate backup with empty Keyspace " + keyspace);
      }
      return;
    }

    if (keyspace == null) {
      tableInfoList =
          tableInfoList.parallelStream()
              .filter(tableInfo -> tableType.equals(tableInfo.getTableType()))
              .collect(Collectors.toList());
      if (tableInfoList.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "No tables to backup inside specified Universe "
                + universe.getUniverseUUID().toString()
                + " and Table Type "
                + tableType.name());
      }
      return;
    }

    // Match if the table is an index or ysql table.
    for (TableInfo tableInfo : tableInfoList) {
      if (tableUuids.contains(
          getUUIDRepresentation(tableInfo.getId().toStringUtf8().replace("-", "")))) {
        if (tableInfo.hasRelationType()
            && tableInfo.getRelationType() == RelationType.INDEX_TABLE_RELATION) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot backup index table " + tableInfo.getName());
        } else if (tableInfo.hasTableType()
            && tableInfo.getTableType() == TableType.PGSQL_TABLE_TYPE) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot backup ysql table " + tableInfo.getName());
        }
      }
    }
  }

  @VisibleForTesting
  void validateTables(List<UUID> tableUuids, Universe universe) {
    if (tableUuids.isEmpty()) {
      return;
    }

    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    // Match if the table is an index or ysql table.
    for (TableInfo tableInfo : tableInfoList) {
      if (tableUuids.contains(
          getUUIDRepresentation(tableInfo.getId().toStringUtf8().replace("-", "")))) {
        if (tableInfo.hasRelationType()
            && tableInfo.getRelationType() == RelationType.INDEX_TABLE_RELATION) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot backup index table " + tableInfo.getName());
        } else if (tableInfo.hasTableType()
            && tableInfo.getTableType() == TableType.PGSQL_TABLE_TYPE) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot backup ysql table " + tableInfo.getName());
        }
      }
    }
  }

  private List<TableInfo> getTableInfosOrEmpty(Universe universe) {
    final String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses.isEmpty()) {
      LOG.warn("Masters are not currently queryable.");
      return Collections.emptyList();
    }

    YBClient client = null;
    try {
      String certificate = universe.getCertificateNodetoNode();
      client = ybService.getClient(masterAddresses, certificate);
      return client.getTablesList().getTableInfoList();
    } catch (Exception e) {
      LOG.warn(e.toString());
      return Collections.emptyList();
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
  }

  @ApiOperation(
      value = "YbaApi Internal. List all tablespaces",
      nickname = "getAllTableSpaces",
      notes = "Get a list of all tablespaces of a given universe",
      response = TableSpaceInfo.class,
      responseContainer = "List")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result listTableSpaces(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    final String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses.isEmpty()) {
      String errMsg = "Expected error. Masters are not currently queryable.";
      LOG.warn(errMsg);
      return ok(errMsg);
    }
    List<TableSpaceInfo> tableSpaceInfoRespList =
        tableHandler.listTableSpaces(customerUUID, universeUUID);
    return PlatformResults.withData(tableSpaceInfoRespList);
  }

  /**
   * This API starts a task for tablespaces creation.
   *
   * @param customerUUID UUID of the customer owning the universe.
   * @param universeUUID UUID of the universe in which the tablespaces will be created.
   */
  @ApiOperation(
      value = "YbaApi Internal. Create tableSpaces",
      nickname = "createTableSpaces",
      response = YBPTask.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.0.0")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "CreateTableSpacesRequest",
        required = true,
        dataType = "com.yugabyte.yw.forms.CreateTablespaceParams",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result createTableSpaces(UUID customerUUID, UUID universeUUID, Http.Request request) {
    CreateTablespaceParams tablespacesInfo =
        parseJsonAndValidate(request, CreateTablespaceParams.class);
    UUID taskUUID = tableHandler.createTableSpaces(customerUUID, universeUUID, tablespacesInfo);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.CreateTableSpaces,
            taskUUID);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }
}
