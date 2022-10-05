// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.Common.CloudType.aws;
import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;
import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static com.yugabyte.yw.forms.TableDefinitionTaskParams.createFromResponse;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTableSpaces;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTableFromUniverse;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceQueryResponse;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.CreateTablespaceParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TableDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponse;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.BiConsumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import lombok.extern.jackson.Jacksonized;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.RelationType;
import play.Environment;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Table management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class TablesController extends AuthenticatedController {

  private static final Logger LOG = LoggerFactory.getLogger(TablesController.class);

  private static final String MASTERS_UNAVAILABLE_ERR_MSG =
      "Expected error. Masters are not currently queryable.";

  private static final String PARTITION_QUERY_PATH = "queries/fetch_table_partitions.sql";

  private static final String MASTER_LEADER_TIMEOUT_CONFIG_PATH =
      "yb.wait_for_master_leader_timeout";

  Commissioner commissioner;

  private final YBClientService ybService;

  private final MetricQueryHelper metricQueryHelper;

  private final CustomerConfigService customerConfigService;

  private final NodeUniverseManager nodeUniverseManager;

  private final Environment environment;

  private final Config config;

  @Inject
  public TablesController(
      Commissioner commissioner,
      YBClientService service,
      MetricQueryHelper metricQueryHelper,
      CustomerConfigService customerConfigService,
      NodeUniverseManager nodeUniverseManager,
      Config config,
      Environment environment) {
    this.commissioner = commissioner;
    this.ybService = service;
    this.metricQueryHelper = metricQueryHelper;
    this.customerConfigService = customerConfigService;
    this.nodeUniverseManager = nodeUniverseManager;
    this.environment = environment;
    this.config = config;
  }

  @ApiOperation(
      value = "Create a YugabyteDB table",
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
  public Result create(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID and universe UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Form<TableDefinitionTaskParams> formData =
        formFactory.getFormDataOrBadRequest(TableDefinitionTaskParams.class);
    TableDefinitionTaskParams taskParams = formData.get();
    // Submit the task to create the table.
    if (taskParams.tableDetails == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Table details can not be null.");
    }
    TableDetails tableDetails = taskParams.tableDetails;
    UUID taskUUID = commissioner.submit(TaskType.CreateCassandraTable, taskParams);
    LOG.info(
        "Submitted create table for {}:{}, task uuid = {}.",
        taskParams.tableUUID,
        tableDetails.tableName,
        taskUUID);

    // Add this task uuid to the user universe.
    // TODO: check as to why we aren't populating the tableUUID from middleware
    // Which means all the log statements above and below are basically logging null?
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Table,
        CustomerTask.TaskType.Create,
        tableDetails.tableName);
    LOG.info(
        "Saved task uuid {} in customer tasks table for table {}:{}.{}",
        taskUUID,
        taskParams.tableUUID,
        tableDetails.keyspace,
        tableDetails.tableName);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Table,
            null,
            Audit.ActionType.Create,
            Json.toJson(formData.rawData()),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "Alter a YugabyteDB table",
      nickname = "alterTable",
      response = Object.class,
      responseContainer = "Map")
  public Result alter(UUID cUUID, UUID uniUUID, UUID tableUUID) {
    return play.mvc.Results.TODO;
  }

  @ApiOperation(value = "Drop a YugabyteDB table", nickname = "dropTable", response = YBPTask.class)
  public Result drop(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);
    final String masterAddresses = universe.getMasterAddresses(true);
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, MASTERS_UNAVAILABLE_ERR_MSG);
    }
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = ybService.getClient(masterAddresses, certificate);
    GetTableSchemaResponse schemaResponse;
    try {
      schemaResponse = client.getTableSchemaByUUID(tableUUID.toString().replace("-", ""));
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    if (schemaResponse == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No table for UUID: " + tableUUID);
    }
    ybService.closeClient(client, masterAddresses);
    DeleteTableFromUniverse.Params taskParams = new DeleteTableFromUniverse.Params();
    taskParams.universeUUID = universeUUID;
    taskParams.expectedUniverseVersion = -1;
    taskParams.tableUUID = tableUUID;
    taskParams.tableName = schemaResponse.getTableName();
    taskParams.keyspace = schemaResponse.getNamespace();
    taskParams.masterAddresses = masterAddresses;

    UUID taskUUID = commissioner.submit(TaskType.DeleteTable, taskParams);
    LOG.info(
        "Submitted delete table for {}:{}, task uuid = {}.",
        taskParams.tableUUID,
        taskParams.getFullName(),
        taskUUID);

    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Table,
        CustomerTask.TaskType.Delete,
        taskParams.getFullName());
    LOG.info(
        "Saved task uuid {} in customer tasks table for table {}:{}",
        taskUUID,
        taskParams.tableUUID,
        taskParams.getFullName());

    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.Table, tableUUID.toString(), Audit.ActionType.Drop, taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "UI_ONLY",
      response = Object.class,
      responseContainer = "Map",
      hidden = true)
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
      value = "List column types",
      notes = "Get a list of all defined column types.",
      response = ColumnDetails.YQLDataType.class,
      responseContainer = "List")
  public Result getYQLDataTypes() {
    return PlatformResults.withData(ColumnDetails.YQLDataType.values());
  }

  @ApiModel(description = "Table information response")
  @Builder
  @Jacksonized
  static class TableInfoResp {

    @ApiModelProperty(value = "Table UUID", accessMode = READ_ONLY)
    public final UUID tableUUID;

    @ApiModelProperty(value = "Keyspace")
    public final String keySpace;

    @ApiModelProperty(value = "Table type")
    public final TableType tableType;

    @ApiModelProperty(value = "Table name")
    public final String tableName;

    @ApiModelProperty(value = "Relation type")
    public final RelationType relationType;

    @ApiModelProperty(value = "SST size in bytes", accessMode = READ_ONLY)
    public final double sizeBytes;

    @ApiModelProperty(value = "WAL size in bytes", accessMode = READ_ONLY)
    public final double walSizeBytes;

    @ApiModelProperty(value = "UI_ONLY", hidden = true)
    public final boolean isIndexTable;

    @ApiModelProperty(value = "Namespace or Schema")
    public final String nameSpace;

    @ApiModelProperty(value = "Table space")
    public final String tableSpace;

    @ApiModelProperty(value = "Parent Table UUID")
    public final UUID parentTableUUID;

    @ApiModelProperty(value = "Postgres schema name of the table", example = "public")
    public final String pgSchemaName;
  }

  @ApiOperation(
      value = "List all tables",
      nickname = "getAllTables",
      notes = "Get a list of all tables in the specified universe",
      response = TableInfoResp.class,
      responseContainer = "List")
  public Result listTables(UUID customerUUID, UUID universeUUID, boolean includeParentTableInfo) {
    if (includeParentTableInfo) {
      return listTablesWithParentTableInfo(customerUUID, universeUUID);
    }
    // Validate customer UUID
    Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);

    final String masterAddresses = universe.getMasterAddresses(true);
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, MASTERS_UNAVAILABLE_ERR_MSG);
    }

    Map<String, TableSizes> tableSizes = getTableSizesOrEmpty(universe);

    String certificate = universe.getCertificateNodetoNode();
    ListTablesResponse response = listTablesOrBadRequest(masterAddresses, certificate);
    List<TableInfo> tableInfoList = response.getTableInfoList();
    List<TableInfoResp> tableInfoRespList = new ArrayList<>(tableInfoList.size());
    for (TableInfo table : tableInfoList) {
      if (!isSystemTable(table) || isSystemRedis(table)) {
        tableInfoRespList.add(buildResponseFromTableInfo(table, null, null, tableSizes).build());
      }
    }
    return PlatformResults.withData(tableInfoRespList);
  }

  private boolean isSystemTable(TableInfo table) {
    return table.getRelationType() == RelationType.SYSTEM_TABLE_RELATION
        || (table.getTableType() == TableType.PGSQL_TABLE_TYPE
            && table.getNamespace().getName().equals(SYSTEM_PLATFORM_DB));
  }

  private boolean isSystemRedis(TableInfo table) {
    return table.getTableType() == TableType.REDIS_TABLE_TYPE
        && table.getRelationType() == RelationType.SYSTEM_TABLE_RELATION
        && table.getNamespace().getName().equals("system_redis")
        && table.getName().equals("redis");
  }

  // Query prometheus for table sizes.
  private Map<String, TableSizes> getTableSizesOrEmpty(Universe universe) {
    try {
      return queryTableSizes(universe.getUniverseDetails().nodePrefix);
    } catch (RuntimeException e) {
      LOG.error(
          "Error querying for table sizes for universe {} from prometheus",
          universe.getUniverseDetails().nodePrefix,
          e);
    }
    return Collections.emptyMap();
  }

  private ListTablesResponse listTablesOrBadRequest(String masterAddresses, String certificate) {
    YBClient client = null;
    ListTablesResponse response;
    try {
      client = ybService.getClient(masterAddresses, certificate);
      checkLeaderMasterAvailability(client);
      response = client.getTablesList();
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
    if (response == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Table list can not be empty");
    }
    return response;
  }

  private void checkLeaderMasterAvailability(YBClient client) {
    long waitForLeaderTimeoutMs = config.getDuration(MASTER_LEADER_TIMEOUT_CONFIG_PATH).toMillis();
    try {
      client.waitForMasterLeader(waitForLeaderTimeoutMs);
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Could not find the master leader");
    }
  }

  /**
   * This API will describe a single table.
   *
   * @param customerUUID UUID of the customer owning the table.
   * @param universeUUID UUID of the universe in which the table resides.
   * @param tableUUID UUID of the table to describe.
   * @return json-serialized description of the table.
   */
  @ApiOperation(
      value = "Describe a table",
      nickname = "describeTable",
      response = TableDefinitionTaskParams.class)
  public Result describe(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    // Validate customer UUID
    Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);
    YBClient client = null;
    String masterAddresses = universe.getMasterAddresses(true);

    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, MASTERS_UNAVAILABLE_ERR_MSG);
    }

    try {
      String certificate = universe.getCertificateNodetoNode();
      client = ybService.getClient(masterAddresses, certificate);
      GetTableSchemaResponse response =
          client.getTableSchemaByUUID(tableUUID.toString().replace("-", ""));

      return PlatformResults.withData(createFromResponse(universe, tableUUID, response));
    } catch (IllegalArgumentException e) {
      LOG.error("Failed to get schema of table " + tableUUID + " in universe " + universeUUID, e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    } catch (Exception e) {
      LOG.error("Failed to get schema of table " + tableUUID + " in universe " + universeUUID, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
  }

  @ApiOperation(
      value = "Create a multi-table backup",
      tags = {"Backups", "Table management"},
      nickname = "createMultiTableBackup",
      response = YBPTask.class)
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
  // Will remove this on completion.
  public Result createMultiTableBackup(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);

    Form<MultiTableBackup.Params> formData =
        formFactory.getFormDataOrBadRequest(MultiTableBackup.Params.class);

    MultiTableBackup.Params taskParams = formData.get();
    if (taskParams.storageConfigUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Missing StorageConfig UUID: " + taskParams.storageConfigUUID);
    }
    customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().backupInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot run Backup task since the universe %s is currently in a locked state.",
              universeUUID.toString()));
    }

    taskParams.universeUUID = universeUUID;
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
              ctx(),
              Audit.TargetType.Universe,
              universeUUID.toString(),
              Audit.ActionType.CreateMultiTableBackup,
              Json.toJson(formData.rawData()));
      return PlatformResults.withData(schedule);
    } else {
      UUID taskUUID = commissioner.submit(TaskType.MultiTableBackup, taskParams);
      LOG.info("Submitted task to universe {}, task uuid = {}.", universe.name, taskUUID);
      CustomerTask.create(
          customer,
          taskParams.universeUUID,
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Create,
          universe.name);
      LOG.info("Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.name);
      auditService()
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.Universe,
              universeUUID.toString(),
              Audit.ActionType.CreateMultiTableBackup,
              Json.toJson(formData.rawData()),
              taskUUID);
      return new YBPTask(taskUUID).asResult();
    }
  }

  @ApiOperation(
      value = "Create a single-table backup",
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
  // Remove this too.
  public Result createBackup(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);

    Form<BackupTableParams> formData = formFactory.getFormDataOrBadRequest(BackupTableParams.class);
    BackupTableParams taskParams = formData.get();

    validateTables(
        Collections.singletonList(tableUUID),
        universe,
        taskParams.getKeyspace(),
        taskParams.backupType);

    customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().backupInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot run Backup task since the universe %s is currently in a locked state.",
              universeUUID.toString()));
    }

    taskParams.universeUUID = universeUUID;
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
          taskParams.getTableName(),
          scheduleUUID);
      auditService()
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.Table,
              tableUUID.toString(),
              Audit.ActionType.CreateSingleTableBackup,
              Json.toJson(formData.rawData()));
      return PlatformResults.withData(schedule);
    } else {
      UUID taskUUID = commissioner.submit(TaskType.BackupUniverse, taskParams);
      LOG.info(
          "Submitted task to backup table {}:{}, task uuid = {}.",
          tableUUID,
          taskParams.getTableName(),
          taskUUID);
      CustomerTask.create(
          customer,
          taskParams.universeUUID,
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Create,
          taskParams.getTableName());
      LOG.info(
          "Saved task uuid {} in customer tasks table for table {}:{}.{}",
          taskUUID,
          tableUUID,
          taskParams.getTableNames(),
          taskParams.getTableName());
      auditService()
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.Table,
              tableUUID.toString(),
              Audit.ActionType.CreateSingleTableBackup,
              Json.toJson(formData.rawData()),
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
      value = "Bulk import data",
      nickname = "bulkImportData",
      notes = "Bulk import data into the specified table. This is currently AWS-only.",
      response = YBPTask.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Bulk import",
        value = "Bulk data to be imported",
        required = true,
        dataType = "com.yugabyte.yw.forms.BulkImportParams",
        paramType = "body")
  })
  public Result bulkImport(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);
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
    Form<BulkImportParams> formData = formFactory.getFormDataOrBadRequest(BulkImportParams.class);
    BulkImportParams taskParams = formData.get();
    if (taskParams.s3Bucket == null || !taskParams.s3Bucket.startsWith("s3://")) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid S3 Bucket provided: " + taskParams.s3Bucket);
    }
    taskParams.universeUUID = universeUUID;

    UUID taskUUID = commissioner.submit(TaskType.ImportIntoTable, taskParams);
    LOG.info(
        "Submitted import into table for {}:{}, task uuid = {}.",
        tableUUID,
        taskParams.getTableName(),
        taskUUID);

    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Table,
        CustomerTask.TaskType.BulkImportData,
        taskParams.getTableName());
    LOG.info(
        "Saved task uuid {} in customer tasks table for table {}:{}.{}",
        taskUUID,
        tableUUID,
        taskParams.getTableName(),
        taskParams.getTableName());

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Table,
            tableUUID.toString(),
            Audit.ActionType.BulkImport,
            Json.toJson(formData.rawData()),
            taskUUID);
    return new YBPTask(taskUUID, tableUUID).asResult();
  }

  @VisibleForTesting
  void validateTables(
      List<UUID> tableUuids, Universe universe, String keyspace, TableType tableType) {

    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    if (keyspace != null && tableUuids.isEmpty()) {
      tableInfoList =
          tableInfoList
              .parallelStream()
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
          tableInfoList
              .parallelStream()
              .filter(tableInfo -> tableType.equals(tableInfo.getTableType()))
              .collect(Collectors.toList());
      if (tableInfoList.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "No tables to backup inside specified Universe "
                + universe.universeUUID.toString()
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
    final String masterAddresses = universe.getMasterAddresses(true);
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

  private Map<String, TableSizes> queryTableSizes(String nodePrefix) {
    HashMap<String, TableSizes> result = new HashMap<>();
    queryAndAppendTableSizeMetric(
        result, "rocksdb_current_version_sst_files_size", nodePrefix, TableSizes::setSstSizeBytes);
    queryAndAppendTableSizeMetric(result, "log_wal_size", nodePrefix, TableSizes::setWalSizeBytes);
    return result;
  }

  private void queryAndAppendTableSizeMetric(
      Map<String, TableSizes> tableSizes,
      String metricName,
      String nodePrefix,
      BiConsumer<TableSizes, Double> fieldSetter) {

    // Execute query and check for errors.
    ArrayList<MetricQueryResponse.Entry> metricValues =
        metricQueryHelper.queryDirect(
            "sum by (table_id) (" + metricName + "{node_prefix=\"" + nodePrefix + "\"})");

    for (final MetricQueryResponse.Entry entry : metricValues) {
      String tableID = entry.labels.get("table_id");
      if (tableID == null
          || tableID.isEmpty()
          || entry.values == null
          || entry.values.size() == 0) {
        continue;
      }
      fieldSetter.accept(
          tableSizes.computeIfAbsent(tableID, k -> new TableSizes()),
          entry.values.get(0).getRight());
    }
  }

  @ApiOperation(
      value = "List all tablespaces",
      nickname = "getAllTableSpaces",
      notes = "Get a list of all tablespaces of a given universe",
      response = TableSpaceInfo.class,
      responseContainer = "List")
  public Result listTableSpaces(UUID customerUUID, UUID universeUUID) {
    Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    final String masterAddresses = universe.getMasterAddresses(true);
    if (masterAddresses.isEmpty()) {
      String errMsg = "Expected error. Masters are not currently queryable.";
      LOG.warn(errMsg);
      return ok(errMsg);
    }

    LOG.info("Fetching table spaces...");
    NodeDetails nodeToUse = null;
    try {
      nodeToUse = CommonUtils.getServerToRunYsqlQuery(universe);
    } catch (IllegalStateException ise) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cluster may not have been initialized yet. Please try later");
    }
    final String fetchTablespaceQuery =
        "select jsonb_agg(t) from (select spcname, spcoptions from pg_catalog.pg_tablespace) as t";
    ShellResponse shellResponse =
        nodeUniverseManager.runYsqlCommand(nodeToUse, universe, "postgres", fetchTablespaceQuery);
    if (!shellResponse.isSuccess()) {
      LOG.warn(
          "Attempt to fetch tablespace info via node {} failed, response {}:{}",
          nodeToUse.nodeName,
          shellResponse.code,
          shellResponse.message);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching TableSpace information");
    }
    String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);
    List<TableSpaceInfo> tableSpaceInfoRespList = new ArrayList<>();
    if (jsonData == null || jsonData.isEmpty()) {
      PlatformResults.withData(tableSpaceInfoRespList);
    }

    LOG.debug("jsonData {}", jsonData);
    try {
      ObjectMapper objectMapper = Json.mapper();
      List<TableSpaceQueryResponse> tablespaceList =
          objectMapper.readValue(jsonData, new TypeReference<List<TableSpaceQueryResponse>>() {});
      tableSpaceInfoRespList =
          tablespaceList
              .stream()
              .filter(x -> !x.tableSpaceName.startsWith("pg_"))
              .map(TableSpaceUtil::parseToTableSpaceInfo)
              .collect(Collectors.toList());
      return PlatformResults.withData(tableSpaceInfoRespList);
    } catch (IOException ioe) {
      LOG.error("Unable to parse fetchTablespaceQuery response {}", jsonData, ioe);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching TableSpace information");
    }
  }

  /**
   * This API starts a task for tablespaces creation.
   *
   * @param customerUUID UUID of the customer owning the universe.
   * @param universeUUID UUID of the universe in which the tablespaces will be created.
   */
  @ApiOperation(
      value = "Create tableSpaces",
      nickname = "createTableSpaces",
      response = YBPTask.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "CreateTableSpacesRequest",
        required = true,
        dataType = "com.yugabyte.yw.forms.CreateTablespaceParams",
        paramType = "body")
  })
  public Result createTableSpaces(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID.
    Universe universe = Universe.getOrBadRequest(universeUUID);

    // Extract tablespaces list.
    CreateTablespaceParams tablespacesInfo = parseJsonAndValidate(CreateTablespaceParams.class);

    TableSpaceUtil.validateTablespaces(tablespacesInfo, universe);

    CreateTableSpaces.Params taskParams = new CreateTableSpaces.Params();
    taskParams.universeUUID = universeUUID;
    taskParams.tablespaceInfos = tablespacesInfo.tablespaceInfos;
    taskParams.expectedUniverseVersion = universe.version;

    UUID taskUUID = commissioner.submit(TaskType.CreateTableSpacesInUniverse, taskParams);
    LOG.info("Submitted create tablespaces task, uuid = {}.", taskUUID);

    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CreateTableSpaces,
        universe.name);

    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {}:{}.",
        taskUUID,
        universeUUID,
        universe.name);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.CreateTableSpaces,
            Json.toJson(request().body().asJson()),
            taskUUID);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }

  private Result listTablesWithParentTableInfo(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID
    Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);

    final String masterAddresses = universe.getMasterAddresses(true);
    if (masterAddresses.isEmpty()) {
      String errMsg = "Expected error. Masters are not currently queryable.";
      LOG.warn(errMsg);
      return ok(errMsg);
    }

    Map<String, TableSizes> tableSizes = getTableSizesOrEmpty(universe);

    String certificate = universe.getCertificateNodetoNode();
    ListTablesResponse response = listTablesOrBadRequest(masterAddresses, certificate);
    List<TableInfo> tableInfoList = response.getTableInfoList();

    Map<String, List<TableInfo>> namespacesToTablesMap =
        tableInfoList.stream().collect(Collectors.groupingBy(ti -> ti.getNamespace().getName()));

    Map<TablePartitionInfoKey, TablePartitionInfo> partitionMap = new HashMap<>();
    for (String namespace : namespacesToTablesMap.keySet()) {
      partitionMap.putAll(fetchTablePartitionInfo(universe, namespace));
    }

    Map<TablePartitionInfoKey, TableInfo> tablePartitionInfoToTableInfoMap =
        tableInfoList
            .stream()
            .collect(
                Collectors.toMap(
                    ti -> new TablePartitionInfoKey(ti.getName(), ti.getNamespace().getName()),
                    Function.identity()));

    List<TableInfoResp> tableInfoRespList = new ArrayList<>(tableInfoList.size());

    for (TableInfo table : tableInfoList) {
      if (!isSystemTable(table) || isSystemRedis(table)) {
        TablePartitionInfoKey partitionInfoKey =
            new TablePartitionInfoKey(table.getName(), table.getNamespace().getName());
        TableInfo parentPartitionInfo = null;
        TablePartitionInfo partitionInfo = null;
        if (partitionMap.containsKey(partitionInfoKey)) {
          // This 'table' is a partition of some table.
          partitionInfo = partitionMap.get(partitionInfoKey);
          parentPartitionInfo =
              tablePartitionInfoToTableInfoMap.get(
                  new TablePartitionInfoKey(partitionInfo.parentTable, partitionInfo.keyspace));
          LOG.debug("Partition {}, Parent {}", partitionInfo, parentPartitionInfo);
        }
        tableInfoRespList.add(
            buildResponseFromTableInfo(table, partitionInfo, parentPartitionInfo, tableSizes)
                .build());
      }
    }

    return PlatformResults.withData(tableInfoRespList);
  }

  private TableInfoResp.TableInfoRespBuilder buildResponseFromTableInfo(
      TableInfo table,
      TablePartitionInfo tablePartitionInfo,
      TableInfo parentTableInfo,
      Map<String, TableSizes> tableSizeMap) {
    String id = table.getId().toStringUtf8();
    String tableKeySpace = table.getNamespace().getName();
    TableInfoResp.TableInfoRespBuilder builder =
        TableInfoResp.builder()
            .tableUUID(getUUIDRepresentation(id))
            .keySpace(tableKeySpace)
            .tableType(table.getTableType())
            .tableName(table.getName())
            .relationType(table.getRelationType())
            .isIndexTable(table.getRelationType() == RelationType.INDEX_TABLE_RELATION);
    TableSizes tableSizes = tableSizeMap.get(id);
    if (tableSizes != null) {
      builder.sizeBytes(tableSizes.getSstSizeBytes());
      builder.walSizeBytes(tableSizes.getWalSizeBytes());
    }
    if (tablePartitionInfo != null) {
      builder.tableSpace(tablePartitionInfo.tablespace);
    }
    if (parentTableInfo != null) {
      builder.parentTableUUID(getUUIDRepresentation(parentTableInfo.getId().toStringUtf8()));
    }
    if (table.hasPgschemaName()) {
      builder.pgSchemaName(table.getPgschemaName());
    }
    return builder;
  }

  private Map<TablePartitionInfoKey, TablePartitionInfo> fetchTablePartitionInfo(
      Universe universe, String dbName) {
    LOG.info("Fetching table partitions...");

    final String fetchPartitionDataQuery =
        FileUtils.readResource(PARTITION_QUERY_PATH, environment);

    NodeDetails randomTServer = CommonUtils.getARandomLiveTServer(universe);
    ShellResponse shellResponse =
        nodeUniverseManager.runYsqlCommand(
            randomTServer, universe, dbName, fetchPartitionDataQuery);
    if (!shellResponse.isSuccess()) {
      LOG.warn(
          "Attempt to fetch table partition info for db {} via node {} failed, response {}:{}",
          dbName,
          randomTServer.nodeName,
          shellResponse.code,
          shellResponse.message);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching Table Partition information");
    }

    LOG.debug("shell response {}", shellResponse);
    String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);

    if (jsonData == null || (jsonData = jsonData.trim()).isEmpty()) {
      return Collections.EMPTY_MAP;
    }
    LOG.debug("jsonData = {}", jsonData);
    try {
      ObjectMapper objectMapper = Json.mapper();
      List<TablePartitionInfo> partitionList =
          objectMapper.readValue(jsonData, new TypeReference<List<TablePartitionInfo>>() {});
      return partitionList
          .stream()
          .map(
              partition -> {
                partition.keyspace = dbName;
                return partition;
              })
          .collect(Collectors.toMap(TablePartitionInfo::getKey, Function.identity()));
    } catch (IOException e) {
      LOG.error("Error while parsing partition query response {}", jsonData, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching Table Partition information");
    }
  }

  @ToString
  private static class TablePartitionInfo {

    public String tableName;

    public String schemaName;

    public String tablespace;

    public String parentTable;

    public String parentSchema;

    public String parentTablespace;

    public String keyspace;

    public TablePartitionInfo() {}

    public TablePartitionInfoKey getKey() {
      return new TablePartitionInfoKey(tableName, keyspace);
    }
  }

  @EqualsAndHashCode
  private static class TablePartitionInfoKey {
    private final String tableName;
    private final String keyspace;

    public TablePartitionInfoKey(String tableName, String keyspace) {
      this.tableName = tableName;
      this.keyspace = keyspace;
    }
  }

  @Data
  private static class TableSizes {
    private double sstSizeBytes;
    private double walSizeBytes;
  }
}
