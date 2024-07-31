/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.BiMap;
import com.google.common.collect.HashBiMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.XClusterScheduler;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigGetResp;
import com.yugabyte.yw.forms.XClusterConfigNeedBootstrapFormData;
import com.yugabyte.yw.forms.XClusterConfigNeedBootstrapPerTableResponse;
import com.yugabyte.yw.forms.XClusterConfigNeedBootstrapPerTableResponse.XClusterNeedBootstrapReason;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigSyncFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
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
import io.swagger.annotations.Authorization;
import java.time.Duration;
import java.time.Instant;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.CommonTypes;
import org.yb.CommonTypes.TableType;
import org.yb.master.MasterDdlOuterClass;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Asynchronous Replication",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class XClusterConfigController extends AuthenticatedController {

  private final Commissioner commissioner;
  private final MetricQueryHelper metricQueryHelper;
  private final BackupHelper backupHelper;
  private final CustomerConfigService customerConfigService;
  private final YBClientService ybService;
  private final RuntimeConfGetter confGetter;
  private final XClusterUniverseService xClusterUniverseService;
  private final AutoFlagUtil autoFlagUtil;
  private final XClusterScheduler xClusterScheduler;
  private final UniverseTableHandler tableHandler;

  @Inject
  public XClusterConfigController(
      Commissioner commissioner,
      MetricQueryHelper metricQueryHelper,
      BackupHelper backupHelper,
      CustomerConfigService customerConfigService,
      YBClientService ybService,
      RuntimeConfGetter confGetter,
      XClusterUniverseService xClusterUniverseService,
      AutoFlagUtil autoFlagUtil,
      XClusterScheduler xClusterScheduler,
      UniverseTableHandler tableHandler) {
    this.commissioner = commissioner;
    this.metricQueryHelper = metricQueryHelper;
    this.backupHelper = backupHelper;
    this.customerConfigService = customerConfigService;
    this.ybService = ybService;
    this.confGetter = confGetter;
    this.xClusterUniverseService = xClusterUniverseService;
    this.autoFlagUtil = autoFlagUtil;
    this.xClusterScheduler = xClusterScheduler;
    this.tableHandler = tableHandler;
  }

  /**
   * API that creates an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "createXClusterConfig",
      notes = "Available since YBA version 2.16.0.0.",
      value = "Create xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_create_form_data",
          value = "XCluster Replication Create Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigCreateFormData",
          paramType = "body",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(path = "sourceUniverseUUID", sourceType = SourceType.REQUEST_BODY)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(path = "targetUniverseUUID", sourceType = SourceType.REQUEST_BODY))
  })
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.16.0.0")
  public Result create(UUID customerUUID, Http.Request request) {
    log.info("Received create XClusterConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigCreateFormData createFormData = parseCreateFormData(customerUUID, request);
    Universe sourceUniverse = Universe.getOrBadRequest(createFormData.sourceUniverseUUID, customer);
    Universe targetUniverse = Universe.getOrBadRequest(createFormData.targetUniverseUUID, customer);
    XClusterConfigTaskBase.checkConfigDoesNotAlreadyExist(
        createFormData.name, createFormData.sourceUniverseUUID, createFormData.targetUniverseUUID);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }

    // Add index tables.
    Map<String, List<String>> mainTableIndexTablesMap =
        XClusterConfigTaskBase.getMainTableIndexTablesMap(
            this.ybService, sourceUniverse, createFormData.tables);
    Set<String> indexTableIdSet =
        mainTableIndexTablesMap.values().stream().flatMap(List::stream).collect(Collectors.toSet());
    createFormData.tables.addAll(indexTableIdSet);
    if (Objects.nonNull(createFormData.bootstrapParams)) {
      mainTableIndexTablesMap.forEach(
          (mainTableId, indexTableIds) -> {
            if (createFormData.bootstrapParams.tables.contains(mainTableId)) {
              createFormData.bootstrapParams.tables.addAll(indexTableIds);
            }
          });
    }

    log.debug(
        "tableIds are {} and BootstrapParams are {}",
        createFormData.tables,
        createFormData.bootstrapParams);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.filterTableInfoListByTableIds(
            sourceTableInfoList, createFormData.tables);

    xClusterCreatePreChecks(
        requestedTableInfoList,
        createFormData.configType,
        sourceUniverse,
        targetUniverse,
        confGetter);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);

    Map<String, String> sourceTableIdTargetTableIdMap =
        XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
            requestedTableInfoList, targetTableInfoList);

    if (createFormData.bootstrapParams != null
        && createFormData.bootstrapParams.allowBootstrap
        && !requestedTableInfoList.isEmpty()) {
      createFormData.bootstrapParams.tables =
          getAllBootstrapRequiredTableForXClusterRequestedTable(
              ybService, requestedTableInfoList, createFormData.tables, sourceUniverse);
    }

    xClusterBootstrappingPreChecks(
        requestedTableInfoList,
        sourceTableInfoList,
        targetUniverse,
        sourceUniverse,
        sourceTableIdTargetTableIdMap,
        ybService,
        createFormData.bootstrapParams,
        null /* currentReplicationGroupName */);

    if (createFormData.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    }

    // Create xCluster config object.
    XClusterConfig xClusterConfig = XClusterConfig.create(createFormData, requestedTableInfoList);
    xClusterConfig.updateIndexTablesFromMainTableIndexTablesMap(mainTableIndexTablesMap);
    verifyTaskAllowed(xClusterConfig, TaskType.CreateXClusterConfig);

    // Submit task to set up xCluster config.
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(
            xClusterConfig,
            createFormData.bootstrapParams,
            requestedTableInfoList,
            mainTableIndexTablesMap,
            sourceTableIdTargetTableIdMap);
    UUID taskUUID = commissioner.submit(TaskType.CreateXClusterConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.Create,
        xClusterConfig.getName());

    log.info("Submitted create XClusterConfig({}), task {}", xClusterConfig.getUuid(), taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.XClusterConfig,
            Objects.toString(xClusterConfig.getUuid(), null),
            Audit.ActionType.Create,
            Json.toJson(createFormData),
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.getUuid()).asResult();
  }

  /**
   * API that gets an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "getXClusterConfig",
      notes = "Available since YBA version 2.16.0.0.",
      value = "Get xcluster config",
      response = XClusterConfigGetResp.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(
                path = "sourceUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.16.0.0")
  public Result get(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received get XClusterConfig({}) request", xclusterConfigUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);

    xClusterScheduler.syncXClusterConfig(xClusterConfig);
    xClusterConfig.refresh();

    // Set tableType if it is UNKNOWN. This is useful for xCluster configs that were created in old
    // versions of YBA and now the customer has updated their YBA version.
    if (xClusterConfig.getTableType().equals(XClusterConfig.TableType.UNKNOWN)) {
      try {
        Universe sourceUniverse =
            Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
            XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
            XClusterConfigTaskBase.filterTableInfoListByTableIds(
                sourceTableInfoList, xClusterConfig.getTableIds());
        xClusterConfig.updateTableType(requestedTableInfoList);
      } catch (Exception e) {
        log.warn(
            "Failed to set tableType for XClusterConfig({}): {}", xClusterConfig, e.getMessage());
        // Ignore any exception because the get API should not fail because of this.
      }
    }

    JsonNode lagMetricData;
    try {
      Set<String> streamIds = xClusterConfig.getStreamIdsWithReplicationSetup();
      log.info(
          "Querying lag metrics for XClusterConfig({}) using CDC stream IDs: {}",
          xClusterConfig.getUuid(),
          streamIds);

      // Query for replication lag
      Map<String, String> metricParams = new HashMap<>();
      String metric = "tserver_async_replication_lag_micros";
      metricParams.put("metrics[0]", metric);
      String startTime = Long.toString(Instant.now().minus(Duration.ofMinutes(1)).getEpochSecond());
      metricParams.put("start", startTime);
      ObjectNode filterJson = Json.newObject();
      Universe sourceUniverse =
          Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
      String nodePrefix = sourceUniverse.getUniverseDetails().nodePrefix;
      filterJson.put("node_prefix", nodePrefix);
      String streamIdFilter = String.join("|", streamIds);
      filterJson.put("stream_id", streamIdFilter);
      metricParams.put("filters", Json.stringify(filterJson));
      lagMetricData =
          metricQueryHelper.query(
              Collections.singletonList(metric), metricParams, Collections.emptyMap());
    } catch (Exception e) {
      String errorMsg =
          String.format(
              "Failed to get lag metric data for XClusterConfig(%s): %s",
              xClusterConfig.getUuid(), e.getMessage());
      log.error(errorMsg);
      lagMetricData = Json.newObject().put("error", errorMsg);
    }

    XClusterConfigTaskBase.setReplicationStatus(
        this.xClusterUniverseService, this.ybService, this.tableHandler, xClusterConfig);

    // Wrap XClusterConfig with lag metric data.
    XClusterConfigGetResp resp = new XClusterConfigGetResp();
    resp.xClusterConfig = xClusterConfig;
    resp.lag = lagMetricData;
    return PlatformResults.withData(resp);
  }

  /**
   * API that edits an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "editXClusterConfig",
      notes = "Available since YBA version 2.16.0.0.",
      value = "Edit xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_edit_form_data",
          value = "XCluster Replication Edit Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigEditFormData",
          paramType = "body",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "sourceUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.16.0.0")
  public Result edit(UUID customerUUID, UUID xclusterConfigUUID, Http.Request request) {
    log.info("Received edit XClusterConfig({}) request", xclusterConfigUUID);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigEditFormData editFormData = parseEditFormData(customerUUID, request);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);
    verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }

    XClusterConfigTaskParams params;
    if (editFormData.tables != null) {
      params =
          getSetTablesTaskParams(
              ybService,
              xClusterConfig,
              sourceUniverse,
              targetUniverse,
              editFormData.tables,
              editFormData.bootstrapParams,
              editFormData.autoIncludeIndexTables,
              editFormData.dryRun);
    } else {
      // If renaming, verify xCluster replication with same name (between same source/target)
      // does not already exist.
      if (editFormData.name != null) {
        XClusterConfigTaskBase.checkConfigDoesNotAlreadyExist(
            editFormData.name,
            xClusterConfig.getSourceUniverseUUID(),
            xClusterConfig.getTargetUniverseUUID());
      }

      // Change role is allowed only for txn xCluster configs.
      if (!xClusterConfig.getType().equals(ConfigType.Txn)
          && (Objects.nonNull(editFormData.sourceRole)
              || Objects.nonNull(editFormData.targetRole))) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Changing xCluster role can be applied only to transactional xCluster configs");
      }

      params =
          new XClusterConfigTaskParams(
              xClusterConfig,
              editFormData,
              null /* requestedTableInfoList */,
              null /* mainTableToAddIndexTablesMap */,
              null /* tableIdsToAdd */,
              Collections.emptyMap() /* sourceTableIdTargetTableIdMap */,
              null /* tableIdsToRemove */);
    }

    if (editFormData.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    }

    // Submit task to edit xCluster config.
    UUID taskUUID = commissioner.submit(TaskType.EditXClusterConfig, params);
    CustomerTask.create(
        customer,
        xClusterConfig.getSourceUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.Edit,
        xClusterConfig.getName());

    log.info("Submitted edit XClusterConfig({}), task {}", xClusterConfig.getUuid(), taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.XClusterConfig,
            xclusterConfigUUID.toString(),
            Audit.ActionType.Edit,
            Json.toJson(editFormData),
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.getUuid()).asResult();
  }

  static XClusterConfigTaskParams getSetDatabasesTaskParams(
      XClusterConfig xClusterConfig,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      Set<String> databaseIds,
      Set<String> databaseIdsToAdd,
      Set<String> databaseIdsToRemove) {

    XClusterConfigEditFormData editForm = new XClusterConfigEditFormData();
    editForm.databases = databaseIds;

    return new XClusterConfigTaskParams(
        xClusterConfig, bootstrapParams, editForm, databaseIdsToAdd, databaseIdsToRemove);
  }

  static XClusterConfigTaskParams getSetTablesTaskParams(
      YBClientService ybService,
      XClusterConfig xClusterConfig,
      Universe sourceUniverse,
      Universe targetUniverse,
      Set<String> tableIds,
      @Nullable XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      boolean autoIncludeIndexTables,
      boolean dryRun) {
    Map<String, List<String>> mainTableToAddIndexTablesMap = null;
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList = null;
    Map<String, String> sourceTableIdTargetTableIdMap = Collections.emptyMap();
    Set<String> currentTableIds = xClusterConfig.getTableIds();
    Pair<Set<String>, Set<String>> tableIdsToAddTableIdsToRemovePair =
        XClusterConfigTaskBase.getTableIdsDiff(currentTableIds, tableIds);
    Set<String> tableIdsToAdd = tableIdsToAddTableIdsToRemovePair.getFirst();
    Set<String> tableIdsToRemove = tableIdsToAddTableIdsToRemovePair.getSecond();
    log.info("tableIdsToAdd are {}; tableIdsToRemove are {}", tableIdsToAdd, tableIdsToRemove);

    // For backward compatibility; if table is in replication, no need for bootstrapping.
    xClusterConfig.updateNeedBootstrapForTables(
        xClusterConfig.getTableIdsWithReplicationSetup(), false /* needBootstrap */);

    if (!tableIdsToAdd.isEmpty()) {
      // Add table to xCluster configs used for DR must have bootstrapParams.
      if (xClusterConfig.isUsedForDr() && Objects.isNull(bootstrapParams)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "To add table to an xCluster config used for DR, bootstrapParams in the payload "
                + "must be passed in");
      }
      mainTableToAddIndexTablesMap =
          XClusterConfigTaskBase.getMainTableIndexTablesMap(ybService, sourceUniverse, tableIds);
      Set<String> indexTableIdSet =
          mainTableToAddIndexTablesMap.values().stream()
              .flatMap(List::stream)
              .collect(Collectors.toSet());
      Set<String> indexTableIdSetToAdd =
          indexTableIdSet.stream()
              .filter(tableId -> !xClusterConfig.getTableIds().contains(tableId))
              .collect(Collectors.toSet());
      if (autoIncludeIndexTables) {
        tableIdsToAdd.addAll(indexTableIdSetToAdd);
      }

      if (Objects.nonNull(bootstrapParams)) {
        mainTableToAddIndexTablesMap.forEach(
            (mainTableId, indexTableIds) -> {
              if (bootstrapParams.tables.contains(mainTableId)) {
                bootstrapParams.tables.addAll(indexTableIds);
              }
            });
      }

      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
      requestedTableInfoList =
          XClusterConfigTaskBase.filterTableInfoListByTableIds(
              sourceTableInfoList, new HashSet<>(CollectionUtils.union(tableIds, tableIdsToAdd)));
      CommonTypes.TableType tableType = XClusterConfigTaskBase.getTableType(requestedTableInfoList);

      XClusterConfigTaskBase.verifyTablesNotInReplication(
          tableIdsToAdd,
          xClusterConfig.getSourceUniverseUUID(),
          xClusterConfig.getTargetUniverseUUID());

      if (!xClusterConfig.getTableType().equals(XClusterConfig.TableType.UNKNOWN)) {
        if (!xClusterConfig.getTableTypeAsCommonType().equals(tableType)) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              String.format(
                  "The xCluster config has a type of %s, but the tables to be added have a "
                      + "type of %s",
                  xClusterConfig.getTableType(),
                  XClusterConfig.XClusterConfigTableTypeCommonTypesTableTypeBiMap.inverse()
                      .get(tableType)));
        }
      }

      // Make sure only supported relations types are passed in by the user.
      Map<Boolean, List<String>> tableIdsPartitionedByIsXClusterSupported =
          XClusterConfigTaskBase.getTableIdsPartitionedByIsXClusterSupported(
              requestedTableInfoList);
      if (!tableIdsPartitionedByIsXClusterSupported.get(false).isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Only the following relation types are supported for xCluster replication: %s; The"
                    + " following tables have different relation types or is a colocated child"
                    + " table: %s",
                XClusterConfigTaskBase.X_CLUSTER_SUPPORTED_TABLE_RELATION_TYPE_SET,
                tableIdsPartitionedByIsXClusterSupported.get(false)));
      }

      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);
      sourceTableIdTargetTableIdMap =
          XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
              requestedTableInfoList, targetTableInfoList);

      Set<String> tablesInReplication =
          new HashSet<>(CollectionUtils.union(tableIds, tableIdsToAdd));
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> allTablesInReplicationInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse, tablesInReplication);
      if (bootstrapParams != null
          && !allTablesInReplicationInfoList.isEmpty()
          && bootstrapParams.allowBootstrap) {
        bootstrapParams.tables =
            getAllBootstrapRequiredTableForXClusterRequestedTable(
                ybService, allTablesInReplicationInfoList, tablesInReplication, sourceUniverse);
      }

      // Verify that parent table will be part of replication if any index table is part of
      // replication
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> allIndexTablesInReplicationInfoList =
          XClusterConfigTaskBase.filterIndexTableInfoList(allTablesInReplicationInfoList);
      Map<String, String> indexTableIdToParentTableIdMap =
          XClusterConfigTaskBase.getIndexTableIdToParentTableIdMap(
              allIndexTablesInReplicationInfoList);
      indexTableIdToParentTableIdMap.forEach(
          (indexTableId, parentTableId) -> {
            if (!tablesInReplication.contains(parentTableId)) {
              throw new PlatformServiceException(
                  BAD_REQUEST,
                  String.format(
                      "The parent table %s of index table %s is not in replication",
                      parentTableId, indexTableId));
            }
          });

      // Index tables should also be part of replication if parent table requires bootstrap for both
      // YSQL and YCQL.
      if (bootstrapParams != null && !CollectionUtils.isEmpty(bootstrapParams.tables)) {
        Set<String> mainTablesInBootstrapParamsIds =
            XClusterConfigTaskBase.filterTableInfoListByTableIds(
                    allTablesInReplicationInfoList, bootstrapParams.tables)
                .stream()
                .filter(tableInfo -> !TableInfoUtil.isIndexTable(tableInfo))
                .map(tableInfo -> XClusterConfigTaskBase.getTableId(tableInfo))
                .collect(Collectors.toSet());

        Map<String, List<String>> mainTableIndexTableInBootstrapMap =
            XClusterConfigTaskBase.getMainTableIndexTablesMap(
                ybService, sourceUniverse, mainTablesInBootstrapParamsIds);
        mainTableIndexTableInBootstrapMap.forEach(
            (mainTableId, indexTableIds) -> {
              if (!tablesInReplication.containsAll(indexTableIds)) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format(
                        "The index tables %s of main table %s are not in replication",
                        indexTableIds, mainTableId));
              }
            });
      }

      // We send null as sourceTableIdTargetTableIdMap because add table does not create tables
      // on the target universe through bootstrapping, and the user is responsible to create the
      // same table on the target universe.
      xClusterBootstrappingPreChecks(
          requestedTableInfoList,
          sourceTableInfoList,
          targetUniverse,
          sourceUniverse,
          sourceTableIdTargetTableIdMap,
          ybService,
          bootstrapParams,
          xClusterConfig.getReplicationGroupName());

      if (!dryRun) {
        // Save the to-be-added tables in the DB.
        xClusterConfig.addTablesIfNotExist(tableIdsToAdd, bootstrapParams);
        xClusterConfig.updateIndexTableForTables(indexTableIdSetToAdd, true /* indexTable */);
      }
    }

    if (!tableIdsToRemove.isEmpty()) {
      // Ignore index tables check on dropped tables as they are not queryable.
      Set<String> droppedTables =
          XClusterConfigTaskBase.getDroppedTableIds(ybService, sourceUniverse, tableIdsToRemove);
      tableIdsToRemove.removeAll(droppedTables);
      // Remove index tables if its main table is removed.
      Map<String, List<String>> mainTableIndexTablesMap =
          XClusterConfigTaskBase.getMainTableIndexTablesMap(
              ybService, sourceUniverse, tableIdsToRemove);
      Set<String> indexTableIdSet =
          mainTableIndexTablesMap.values().stream()
              .flatMap(List::stream)
              .filter(currentTableIds::contains)
              .collect(Collectors.toSet());
      tableIdsToRemove.addAll(indexTableIdSet);
      tableIdsToRemove.addAll(droppedTables);
    }

    if (tableIdsToAdd.isEmpty() && tableIdsToRemove.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No change in the xCluster config table list is detected");
    }

    if (xClusterConfig.getTableIdsWithReplicationSetup().size() + tableIdsToAdd.size()
        == tableIdsToRemove.size()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "The operation to remove tables from replication config will remove all the "
              + "tables in replication which is not allowed; if you want to delete "
              + "replication for all of them, please delete the replication config");
    }

    // Todo: Remove reliance on passing editFormData to the task params.
    XClusterConfigEditFormData editForm = new XClusterConfigEditFormData();
    editForm.tables = tableIds;
    editForm.bootstrapParams = bootstrapParams;

    return new XClusterConfigTaskParams(
        xClusterConfig,
        editForm,
        requestedTableInfoList,
        mainTableToAddIndexTablesMap,
        tableIdsToAdd,
        sourceTableIdTargetTableIdMap,
        tableIdsToRemove);
  }

  /**
   * API that restarts an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "restartXClusterConfig",
      notes = "Available since YBA version 2.16.0.0.",
      value = "Restart xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_restart_form_data",
          value = "XCluster Replication Restart Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigRestartFormData",
          paramType = "body",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "sourceUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.16.0.0")
  public Result restart(
      UUID customerUUID, UUID xClusterConfigUUID, boolean isForceDelete, Http.Request request) {
    log.info(
        "Received restart XClusterConfig({}) request with isForceDelete={}",
        xClusterConfigUUID,
        isForceDelete);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xClusterConfigUUID);
    XClusterConfigRestartFormData restartForm =
        parseRestartFormData(customerUUID, xClusterConfig, request);
    verifyTaskAllowed(xClusterConfig, TaskType.RestartXClusterConfig);
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }

    XClusterConfigTaskParams params =
        getRestartTaskParams(
            ybService,
            xClusterConfig,
            sourceUniverse,
            targetUniverse,
            restartForm.tables,
            restartForm.bootstrapParams,
            restartForm.dryRun,
            isForceDelete,
            false /*forceBootstrap*/);

    if (restartForm.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    }

    // Submit task to edit xCluster config.
    UUID taskUUID = commissioner.submit(TaskType.RestartXClusterConfig, params);
    CustomerTask.create(
        customer,
        xClusterConfig.getSourceUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.Restart,
        xClusterConfig.getName());
    log.info("Submitted restart XClusterConfig({}), task {}", xClusterConfig.getUuid(), taskUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.XClusterConfig,
            xClusterConfigUUID.toString(),
            Audit.ActionType.Restart,
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.getUuid()).asResult();
  }

  static XClusterConfigTaskParams getRestartTaskParams(
      YBClientService ybService,
      XClusterConfig xClusterConfig,
      Universe sourceUniverse,
      Universe targetUniverse,
      Set<String> tableIds,
      RestartBootstrapParams restartBootstrapParams,
      boolean dryRun,
      boolean isForceDelete,
      boolean isForceBootstrap) {

    // Add index tables.
    Map<String, List<String>> mainTableIndexTablesMap =
        XClusterConfigTaskBase.getMainTableIndexTablesMap(ybService, sourceUniverse, tableIds);
    Set<String> indexTableIdSet =
        mainTableIndexTablesMap.values().stream().flatMap(List::stream).collect(Collectors.toSet());
    tableIds.addAll(indexTableIdSet);

    if (!dryRun) {
      xClusterConfig.addTablesIfNotExist(
          indexTableIdSet, null /* tableIdsNeedBootstrap */, true /* areIndexTables */);
    }

    log.debug("tableIds are {}", tableIds);

    XClusterConfigCreateFormData.BootstrapParams bootstrapParams = null;
    if (restartBootstrapParams != null) {
      bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
      bootstrapParams.backupRequestParams = restartBootstrapParams.backupRequestParams;
      bootstrapParams.tables = tableIds;
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.filterTableInfoListByTableIds(sourceTableInfoList, tableIds);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);
    Map<String, String> sourceTableIdTargetTableIdMap =
        XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
            requestedTableInfoList, targetTableInfoList);

    xClusterBootstrappingPreChecks(
        requestedTableInfoList,
        sourceTableInfoList,
        targetUniverse,
        sourceUniverse,
        sourceTableIdTargetTableIdMap,
        ybService,
        bootstrapParams,
        xClusterConfig.getReplicationGroupName());

    return new XClusterConfigTaskParams(
        xClusterConfig,
        bootstrapParams,
        requestedTableInfoList,
        mainTableIndexTablesMap,
        sourceTableIdTargetTableIdMap,
        isForceDelete,
        isForceBootstrap);
  }

  static XClusterConfigTaskParams getDbScopedRestartTaskParams(
      YBClientService ybService,
      XClusterConfig xClusterConfig,
      Universe sourceUniverse,
      Universe targetUniverse,
      Set<String> dbIds,
      RestartBootstrapParams restartBootstrapParams,
      boolean dryRun,
      boolean isForceDelete,
      boolean isForceBootstrap) {

    XClusterConfigCreateFormData.BootstrapParams bootstrapParams = null;
    if (restartBootstrapParams != null) {
      bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
      bootstrapParams.backupRequestParams = restartBootstrapParams.backupRequestParams;
      bootstrapParams.tables = new HashSet<>();
    }

    return new XClusterConfigTaskParams(
        xClusterConfig,
        bootstrapParams,
        CollectionUtils.isEmpty(dbIds) ? xClusterConfig.getDbIds() : dbIds,
        null,
        isForceBootstrap);
  }

  /**
   * API that deletes an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "deleteXClusterConfig",
      notes = "Available since YBA version 2.16.0.0.",
      value = "Delete xcluster config",
      response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "sourceUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.16.0.0")
  public Result delete(
      UUID customerUUID, UUID xClusterConfigUuid, boolean isForceDelete, Http.Request request) {
    log.info(
        "Received delete XClusterConfig({}) request with isForceDelete={}",
        xClusterConfigUuid,
        isForceDelete);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xClusterConfigUuid);
    verifyTaskAllowed(xClusterConfig, TaskType.DeleteXClusterConfig);

    if (xClusterConfig.isUsedForDr()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot delete an xCluster config that is used for DR: this config is used for "
                  + "DR with uuid %s",
              xClusterConfig.getDrConfig().getUuid()));
    }

    Universe sourceUniverse = null;
    Universe targetUniverse = null;
    if (xClusterConfig.getSourceUniverseUUID() != null) {
      sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    }
    if (xClusterConfig.getTargetUniverseUUID() != null) {
      targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);
    }

    // Submit task to delete xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig, isForceDelete);
    UUID taskUUID = commissioner.submit(TaskType.DeleteXClusterConfig, params);
    if (sourceUniverse != null) {
      CustomerTask.create(
          customer,
          sourceUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.XClusterConfig,
          CustomerTask.TaskType.Delete,
          xClusterConfig.getName());
    } else if (targetUniverse != null) {
      CustomerTask.create(
          customer,
          targetUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.XClusterConfig,
          CustomerTask.TaskType.Delete,
          xClusterConfig.getName());
    }
    log.info("Submitted delete XClusterConfig({}), task {}", xClusterConfig.getUuid(), taskUUID);

    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.XClusterConfig,
            xClusterConfigUuid.toString(),
            Audit.ActionType.Delete,
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfigUuid).asResult();
  }

  /**
   * API that syncs target universe xCluster replication configuration with platform state.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "syncXClusterConfig",
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.23.0.0.</b></p>"
              + " Sync xcluster config (V2) instead.",
      value = "Sync xcluster config - deprecated",
      response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation =
            @Resource(path = "targetUniverseUUID", sourceType = SourceType.REQUEST_BODY))
  })
  @Deprecated
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.23.0.0")
  public Result sync(UUID customerUUID, UUID targetUniverseUUID, Http.Request request) {
    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigTaskParams params;
    Universe targetUniverse;
    if (targetUniverseUUID != null) {
      log.info("Received sync XClusterConfig request for universe({})", targetUniverseUUID);
      targetUniverse = Universe.getOrBadRequest(targetUniverseUUID, customer);
      params = new XClusterConfigTaskParams(targetUniverseUUID);
    } else {
      JsonNode requestBody = request.body().asJson();
      XClusterConfigSyncFormData formData =
          formFactory.getFormDataOrBadRequest(requestBody, XClusterConfigSyncFormData.class);
      log.info(
          "Received sync XClusterConfig request for universe({}) replicationGroupName({})",
          formData.targetUniverseUUID,
          formData.replicationGroupName);
      targetUniverse = Universe.getOrBadRequest(formData.targetUniverseUUID, customer);
      params = new XClusterConfigTaskParams(formData);
    }

    UUID taskUUID = commissioner.submit(TaskType.SyncXClusterConfig, params);
    CustomerTask.create(
        customer,
        targetUniverse.getUniverseUUID(),
        taskUUID,
        TargetType.XClusterConfig,
        CustomerTask.TaskType.Sync,
        targetUniverse.getName());

    log.info(
        "Submitted sync XClusterConfig for universe({}), task {}", targetUniverseUUID, taskUUID);

    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            targetUniverse.getUniverseUUID().toString(),
            Audit.ActionType.SyncXClusterConfig,
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that syncs xCluster replication configuration with platform state.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "syncXClusterConfigV2",
      notes = "Available since YBA version 2.23.0.0",
      value = "Sync xcluster config (V2)",
      response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "sourceUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.23.0.0")
  public Result syncXClusterConfig(UUID customerUUID, UUID xClusterUUID, Http.Request request) {
    log.info("Received sync XClusterConfig({}) request", xClusterUUID);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xClusterUUID);
    verifyTaskAllowed(xClusterConfig, TaskType.SyncXClusterConfig);

    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);
    XClusterConfigSyncFormData syncFormData = new XClusterConfigSyncFormData();
    syncFormData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    syncFormData.replicationGroupName = xClusterConfig.getReplicationGroupName();
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(syncFormData);
    UUID taskUUID = commissioner.submit(TaskType.SyncXClusterConfig, params);
    CustomerTask.create(
        customer,
        targetUniverse.getUniverseUUID(),
        taskUUID,
        TargetType.XClusterConfig,
        CustomerTask.TaskType.Sync,
        xClusterConfig.getName());

    log.info("Submitted sync XClusterConfig({}), task {}", xClusterUUID, taskUUID);

    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.XClusterConfig,
            xClusterUUID.toString(),
            Audit.ActionType.SyncXClusterConfig,
            taskUUID);
    return new YBPTask(taskUUID, xClusterUUID).asResult();
  }

  /**
   * It checks whether a table requires bootstrap before setting up xCluster replication. Currently,
   * only one table can be checked at each call.
   *
   * @return An object of Result containing a map of tableId to a boolean showing whether it needs
   *     bootstrapping
   */
  @ApiOperation(
      nickname = "needBootstrapTable",
      notes = "WARNING: This is a preview API that could change.",
      value = "Whether tables need bootstrap before setting up cross cluster replication",
      response = Map.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_need_bootstrap_form_data",
          value = "XCluster Need Bootstrap Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigNeedBootstrapFormData",
          paramType = "body",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(path = "targetUniverseUUID", sourceType = SourceType.REQUEST_BODY))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.16.0.0")
  public Result needBootstrapTable(
      UUID customerUuid,
      UUID sourceUniverseUuid,
      String configTypeString,
      boolean includeDetails,
      Http.Request request) {
    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUuid);
    XClusterConfigNeedBootstrapFormData needBootstrapFormData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), XClusterConfigNeedBootstrapFormData.class);
    Universe sourceUniverse = Universe.getOrBadRequest(sourceUniverseUuid, customer);
    needBootstrapFormData.tables =
        XClusterConfigTaskBase.convertUuidStringsToIdStringSet(needBootstrapFormData.tables);

    log.info(
        "Received needBootstrapTable request for sourceUniverseUuid={}, configTypeString={},"
            + " includeDetails={} with body={}",
        sourceUniverseUuid,
        configTypeString,
        includeDetails,
        needBootstrapFormData);

    if (Objects.nonNull(configTypeString)) {
      log.warn("The configTypeString parameter is deprecated and will be removed in the future");
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
    Map<String, MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableIdToTableInfoMap =
        XClusterConfigTaskBase.getTableIdToTableInfoMap(sourceTableInfoList);
    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        namespaceIdToTableInfoListMap =
            XClusterConfigTaskBase.groupByNamespaceId(sourceTableInfoList);
    log.debug("namespaceIdToTableInfoListMap is {}", namespaceIdToTableInfoListMap);
    Map<String, List<String>> sourceUniverseMainTableIndexTablesMap;
    // For universes newer than or equal to 2.21.1.0-b168, we use the following method to improve
    // performance.
    if (Util.compareYbVersions(
            "2.21.1.0-b168",
            sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
            true)
        <= 0) {
      sourceUniverseMainTableIndexTablesMap =
          XClusterConfigTaskBase.getMainTableIndexTablesMap(sourceUniverse, sourceTableInfoList);
    } else {
      log.warn(
          "Universe {} does not support indexed_table_id in the ListTable RPC, the response may"
              + " take time to be generated. Please consider upgrading the universe to a newer"
              + " version.",
          sourceUniverseUuid);
      sourceUniverseMainTableIndexTablesMap =
          XClusterConfigTaskBase.getMainTableIndexTablesMap(
              this.ybService,
              sourceUniverse,
              XClusterConfigTaskBase.getTableIds(sourceTableInfoList));
    }
    log.debug("sourceUniverseMainTableIndexTablesMap is {}", sourceUniverseMainTableIndexTablesMap);

    Set<String> allTableIds = new HashSet<>(needBootstrapFormData.tables);

    Set<String> addedNamespaceIds = new HashSet<>();
    needBootstrapFormData.tables.forEach(
        tableId -> {
          MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo =
              sourceTableIdToTableInfoMap.get(tableId);
          if (Objects.isNull(tableInfo)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format("The table %s does not exist on the source universe", tableId));
          }
          CommonTypes.TableType tableType = tableInfo.getTableType();
          if (tableType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)) {
            // For YSQL, add other tables in the same namespace.
            String namespaceId = tableInfo.getNamespace().getId().toStringUtf8();
            if (addedNamespaceIds.contains(namespaceId)) {
              return;
            }
            addedNamespaceIds.add(namespaceId);
            allTableIds.addAll(
                namespaceIdToTableInfoListMap.get(namespaceId).stream()
                    .map(XClusterConfigTaskBase::getTableId)
                    .collect(Collectors.toSet()));
          } else if (tableType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)) {
            // For YCQL, add other index tables and the main table.
            String mainTableId;
            if (TableInfoUtil.isIndexTable(tableInfo)) {
              mainTableId = tableInfo.getIndexedTableId();
            } else {
              mainTableId = XClusterConfigTaskBase.getTableId(tableInfo);
            }
            allTableIds.add(mainTableId);
            List<String> indexTables =
                sourceUniverseMainTableIndexTablesMap.getOrDefault(
                    mainTableId, Collections.emptyList());
            allTableIds.addAll(indexTables);
          } else {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "The table type %s of tableId %s is not supported for xCluster replication",
                    tableType, tableId));
          }
        });
    log.debug("The set to be tested for need bootstrap is: {}", allTableIds);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        sourceTableInfoList.stream()
            .filter(tableInfo -> allTableIds.contains(tableInfo.getId().toStringUtf8()))
            .collect(Collectors.toList());

    Map<String, XClusterConfigNeedBootstrapPerTableResponse>
        tableIdToNeedBootstrapPerTableResponseMap = new HashMap<>();
    allTableIds.forEach(
        tableId ->
            tableIdToNeedBootstrapPerTableResponseMap.put(
                tableId, new XClusterConfigNeedBootstrapPerTableResponse()));

    // If tables do not exist on the target universe, bootstrapping is required.
    Optional<Set<String>> sourceTableIdsWithNoTableOnTargetUniverseOptional = Optional.empty();
    Optional<BiMap<String, String>> sourceTableIdTargetTableIdBiMapOptional = Optional.empty();
    if (Objects.nonNull(needBootstrapFormData.targetUniverseUUID)) {
      Universe targetUniverse =
          Universe.getOrBadRequest(needBootstrapFormData.targetUniverseUUID, customer);
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTablesInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);
      Map<String, String> sourceTableIdTargetTableIdMap =
          XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
              requestedTableInfoList, targetTablesInfoList);

      sourceTableIdsWithNoTableOnTargetUniverseOptional =
          Optional.of(
              sourceTableIdTargetTableIdMap.entrySet().stream()
                  .filter(entry -> Objects.isNull(entry.getValue()))
                  .map(Entry::getKey)
                  .collect(Collectors.toSet()));
      log.debug(
          "sourceTableIdsWithNoTableOnTargetUniverse is {}",
          sourceTableIdsWithNoTableOnTargetUniverseOptional.get());

      sourceTableIdTargetTableIdBiMapOptional =
          Optional.of(
              HashBiMap.create(
                  sourceTableIdTargetTableIdMap.entrySet().stream()
                      .filter(entry -> Objects.nonNull(entry.getValue()))
                      .collect(Collectors.toMap(Entry::getKey, Entry::getValue))));
      log.debug(
          "sourceTableIdTargetTableIdBiMapOptional is {}",
          sourceTableIdTargetTableIdBiMapOptional.get());
    }

    try {
      Map<String, Boolean> isBootstrapRequiredMap;
      isBootstrapRequiredMap =
          this.xClusterUniverseService.isBootstrapRequired(
              allTableIds, null /* xClusterConfig */, sourceUniverseUuid);

      // If isBootstrapRequiredMap is true for a table, it is not empty on the source universe.
      Set<String> tableIdsWithData =
          isBootstrapRequiredMap.entrySet().stream()
              .filter(Entry::getValue)
              .map(Entry::getKey)
              .collect(Collectors.toSet());
      tableIdsWithData.forEach(
          tableId -> {
            tableIdToNeedBootstrapPerTableResponseMap
                .get(tableId)
                .addReason(XClusterNeedBootstrapReason.TABLE_HAS_DATA);
          });

      // Merge with the results from sourceTableIdsWithNoTableOnTargetUniverse if required.
      sourceTableIdsWithNoTableOnTargetUniverseOptional.ifPresent(
          sourceTableIdsWithNoTableOnTargetUniverse ->
              sourceTableIdsWithNoTableOnTargetUniverse.forEach(
                  tableId -> {
                    isBootstrapRequiredMap.put(tableId, true);
                    tableIdToNeedBootstrapPerTableResponseMap
                        .get(tableId)
                        .addReason(XClusterNeedBootstrapReason.TABLE_MISSING_ON_TARGET);
                  }));

      // If an index table needs bootstrapping, its main table needs bootstrapping too because
      // backup/restore can be done only on the main tables and the index tables will be
      // automatically included.
      sourceUniverseMainTableIndexTablesMap.forEach(
          (mainTableId, indexTableIds) -> {
            if (isBootstrapRequiredMap.containsKey(mainTableId)
                && isBootstrapRequiredMap.entrySet().stream()
                    .filter(entry -> indexTableIds.contains(entry.getKey()))
                    .anyMatch(Entry::getValue)) {
              isBootstrapRequiredMap.put(mainTableId, true);
            }
          });

      sourceTableIdTargetTableIdBiMapOptional.ifPresent(
          sourceTableIdTargetTableIdBiMap -> {
            List<XClusterConfig> xClusterConfigsInReverseDirection =
                XClusterConfig.getBetweenUniverses(
                    needBootstrapFormData.targetUniverseUUID, sourceUniverseUuid);
            // Detect YSQL namespaces in bidirectional replication.
            Set<String> ysqlSourceNamespaceIdsInReverseReplication = new HashSet<>();
            xClusterConfigsInReverseDirection.stream()
                .filter(
                    xClusterConfigInReverseDirection ->
                        xClusterConfigInReverseDirection
                            .getTableType()
                            .equals(XClusterConfig.TableType.YSQL))
                .forEach(
                    xClusterConfigInReverseDirection ->
                        xClusterConfigInReverseDirection.getTableIds().stream()
                            .map(
                                tableId ->
                                    sourceTableIdToTableInfoMap.get(
                                        sourceTableIdTargetTableIdBiMap.inverse().get(tableId)))
                            .filter(Objects::nonNull)
                            .forEach(
                                tableInfo ->
                                    ysqlSourceNamespaceIdsInReverseReplication.add(
                                        tableInfo.getNamespace().getId().toStringUtf8())));

            // Detect YCQL index group in bidirectional replication.
            Set<String> ycqlSourceTableIdsIndexGroupInReverseReplication = new HashSet<>();
            xClusterConfigsInReverseDirection.stream()
                .filter(
                    xClusterConfigInReverseDirection ->
                        xClusterConfigInReverseDirection
                            .getTableType()
                            .equals(XClusterConfig.TableType.YCQL))
                .forEach(
                    xClusterConfigInReverseDirection ->
                        xClusterConfigInReverseDirection.getTableIds().stream()
                            .map(
                                tableId ->
                                    sourceTableIdToTableInfoMap.get(
                                        sourceTableIdTargetTableIdBiMap.inverse().get(tableId)))
                            .filter(Objects::nonNull)
                            .forEach(
                                tableInfo -> {
                                  String mainTableId;
                                  if (TableInfoUtil.isIndexTable(tableInfo)) {
                                    mainTableId = tableInfo.getIndexedTableId();
                                  } else {
                                    mainTableId = XClusterConfigTaskBase.getTableId(tableInfo);
                                  }
                                  List<String> indexTables =
                                      sourceUniverseMainTableIndexTablesMap.getOrDefault(
                                          mainTableId, Collections.emptyList());
                                  Stream.concat(Stream.of(mainTableId), indexTables.stream())
                                      .forEach(
                                          ycqlSourceTableIdsIndexGroupInReverseReplication::add);
                                }));

            // Bootstrapping cannot be done for bidirectional replication.
            allTableIds.forEach(
                tableId -> {
                  MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo =
                      sourceTableIdToTableInfoMap.get(tableId);
                  if (tableInfo.getTableType().equals(TableType.PGSQL_TABLE_TYPE)) {
                    String namespaceId = tableInfo.getNamespace().getId().toStringUtf8();
                    if (ysqlSourceNamespaceIdsInReverseReplication.contains(namespaceId)) {
                      tableIdToNeedBootstrapPerTableResponseMap
                          .get(tableId)
                          .addReason(XClusterNeedBootstrapReason.BIDIRECTIONAL_REPLICATION);
                    }
                  } else if (tableInfo.getTableType().equals(TableType.YQL_TABLE_TYPE)) {
                    if (ycqlSourceTableIdsIndexGroupInReverseReplication.contains(tableId)) {
                      tableIdToNeedBootstrapPerTableResponseMap
                          .get(tableId)
                          .addReason(XClusterNeedBootstrapReason.BIDIRECTIONAL_REPLICATION);
                    }
                  }
                });
          });

      if (includeDetails) {
        return PlatformResults.withData(
            tableIdToNeedBootstrapPerTableResponseMap.entrySet().stream()
                .filter(entry -> needBootstrapFormData.tables.contains(entry.getKey()))
                .collect(Collectors.toMap(Entry::getKey, Entry::getValue)));
      }

      // The response should include only the requested tables.
      return PlatformResults.withData(
          isBootstrapRequiredMap.entrySet().stream()
              .filter(entry -> needBootstrapFormData.tables.contains(entry.getKey()))
              .collect(Collectors.toMap(Entry::getKey, Entry::getValue)));
    } catch (Exception e) {
      log.error("XClusterConfigTaskBase.isBootstrapRequired hit error: ", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Exception happened while running IsBootstrapRequired API: %s", e.getMessage()));
    }
  }

  /**
   * It checks whether a table within a replication has fallen far behind and need bootstrap to
   * continue replication.
   *
   * @return An object of Result containing a map of tableId to a boolean showing whether it needs
   *     bootstrapping
   */
  @ApiOperation(
      nickname = "NeedBootstrapXClusterConfig",
      notes = "YbaApi Internal.",
      value =
          "Whether tables in an xCluster replication config have fallen far behind"
              + " and need bootstrap",
      response = Map.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_need_bootstrap_form_data",
          value = "XCluster Need Bootstrap Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigNeedBootstrapFormData",
          paramType = "body",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(
                path = "sourceUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "xcluster_configs",
                columnName = "uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  public Result needBootstrap(UUID customerUuid, UUID xClusterConfigUuid, Http.Request request) {
    log.info("Received needBootstrap request for xClusterConfigUuid={}", xClusterConfigUuid);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUuid);
    XClusterConfigNeedBootstrapFormData needBootstrapFormData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), XClusterConfigNeedBootstrapFormData.class);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xClusterConfigUuid);
    needBootstrapFormData.tables =
        XClusterConfigTaskBase.convertUuidStringsToIdStringSet(needBootstrapFormData.tables);

    try {
      Map<String, Boolean> isBootstrapRequiredMap =
          this.xClusterUniverseService.isBootstrapRequired(
              needBootstrapFormData.tables, xClusterConfig, xClusterConfig.getSourceUniverseUUID());
      return PlatformResults.withData(isBootstrapRequiredMap);
    } catch (Exception e) {
      log.error("XClusterConfigTaskBase.isBootstrapRequired hit error : {}", e.getMessage());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Exception happened while running IsBootstrapRequired API: %s", e.getMessage()));
    }
  }

  private XClusterConfigCreateFormData parseCreateFormData(
      UUID customerUUID, Http.Request request) {
    log.debug("Request body to create an xCluster config is {}", request.body().asJson());
    XClusterConfigCreateFormData formData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), XClusterConfigCreateFormData.class);

    if (Objects.equals(formData.sourceUniverseUUID, formData.targetUniverseUUID)) {
      throw new IllegalArgumentException(
          String.format(
              "Source and target universe cannot be the same: both are %s",
              formData.sourceUniverseUUID));
    }

    formData.tables = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.tables);

    // Validate bootstrap parameters if there is any.
    if (formData.bootstrapParams != null) {
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams = formData.bootstrapParams;
      bootstrapParams.tables =
          XClusterConfigTaskBase.convertUuidStringsToIdStringSet(bootstrapParams.tables);
      // Fail early if parameters are invalid for bootstrapping.
      if (!bootstrapParams.tables.isEmpty()) {
        validateBackupRequestParamsForBootstrapping(
            bootstrapParams.backupRequestParams, customerUUID);
      }
    }

    if (formData.tables.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "tables in the request cannot be empty");
    }

    return formData;
  }

  private XClusterConfigEditFormData parseEditFormData(UUID customerUUID, Http.Request request) {
    log.debug("Request body to edit an xCluster config is {}", request.body().asJson());
    XClusterConfigEditFormData formData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), XClusterConfigEditFormData.class);

    // Ensure exactly one edit form field is specified
    int numEditOps = 0;
    numEditOps += formData.name != null ? 1 : 0;
    numEditOps += formData.status != null ? 1 : 0;
    numEditOps += (formData.tables != null && !formData.tables.isEmpty()) ? 1 : 0;
    numEditOps += formData.sourceRole != null ? 1 : 0;
    numEditOps += formData.targetRole != null ? 1 : 0;
    if (numEditOps == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Must specify an edit operation");
    } else if (numEditOps > 1) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Exactly one edit request is allowed in one call");
    }

    if (formData.tables != null && !formData.tables.isEmpty()) {
      formData.tables = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.tables);
      // Validate bootstrap parameters if there is any.
      if (formData.bootstrapParams != null) {
        XClusterConfigCreateFormData.BootstrapParams bootstrapParams = formData.bootstrapParams;
        bootstrapParams.tables =
            XClusterConfigTaskBase.convertUuidStringsToIdStringSet(bootstrapParams.tables);
        // Fail early if parameters are invalid for bootstrapping.
        if (bootstrapParams.tables.size() > 0) {
          validateBackupRequestParamsForBootstrapping(
              bootstrapParams.backupRequestParams, customerUUID);
        }
      }
    }

    return formData;
  }

  private XClusterConfigRestartFormData parseRestartFormData(
      UUID customerUUID, XClusterConfig xClusterConfig, Http.Request request) {
    log.debug("Request body to restart an xCluster config is {}", request.body().asJson());
    XClusterConfigRestartFormData formData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), XClusterConfigRestartFormData.class);

    formData.tables = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.tables);

    if (formData.bootstrapParams != null) {
      validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);
    }

    Set<String> tableIds = xClusterConfig.getTableIds();
    if (!formData.tables.isEmpty()) {
      // Make sure the selected tables are already part of the xCluster config.
      Set<String> notFoundTableIds = new HashSet<>();
      for (String tableId : formData.tables) {
        if (!tableIds.contains(tableId)) {
          notFoundTableIds.add(tableId);
        }
      }
      if (!notFoundTableIds.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "These selected table ids are not part of the xCluster config %s",
                notFoundTableIds));
      }

      if ((xClusterConfig.getStatus() == XClusterConfig.XClusterConfigStatusType.Failed
              || xClusterConfig.getStatus() == XClusterConfigStatusType.Initialized)
          && formData.tables.size() < xClusterConfig.getTableIdsExcludeIndexTables().size()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Per DB/table xCluster config restart cannot be done because the creation of the "
                + "xCluster config failed or is in initialized state; please do not specify the "
                + "`tables` field so the whole xCluster config restarts");
      }
    } else {
      formData.tables = tableIds;
    }

    return formData;
  }

  public static void verifyTaskAllowed(XClusterConfig xClusterConfig, TaskType taskType) {
    if (!XClusterConfigTaskBase.isTaskAllowed(xClusterConfig, taskType)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s task is not allowed; with status `%s`, the allowed tasks are %s",
              taskType,
              xClusterConfig.getStatus(),
              XClusterConfigTaskBase.getAllowedTasks(xClusterConfig)));
    }
  }

  private void validateBackupRequestParamsForBootstrapping(
      BootstrapParams.BootstrapBackupParams bootstrapBackupParams, UUID customerUUID) {
    XClusterConfigTaskBase.validateBackupRequestParamsForBootstrapping(
        customerConfigService, backupHelper, bootstrapBackupParams, customerUUID);
  }

  public static void certsForCdcDirGFlagCheck(Universe sourceUniverse, Universe targetUniverse) {
    // If the certs_for_cdc_dir gflag is not set, and it is required, tell the user to set it
    // before running this task.
    try {
      if (XClusterConfigTaskBase.getSourceCertificateIfNecessary(sourceUniverse, targetUniverse)
              .isPresent()
          && targetUniverse.getUniverseDetails().getSourceRootCertDirPath() == null) {
        throw new PlatformServiceException(
            METHOD_NOT_ALLOWED,
            String.format(
                "The %s gflag is required, but it is not set. Please use `Edit Flags` "
                    + "feature to set %s gflag to %s on the target universe",
                XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG,
                XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG,
                XClusterConfigTaskBase.getProducerCertsDir(
                    targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.provider)));
      }
    } catch (IllegalArgumentException e) {
      throw new PlatformServiceException(METHOD_NOT_ALLOWED, e.getMessage());
    }
  }

  public static void transactionalXClusterPreChecks(
      RuntimeConfGetter confGetter,
      Universe sourceUniverse,
      Universe targetUniverse,
      CommonTypes.TableType tableType) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.transactionalXClusterEnabled)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Support for transactional xCluster configs is disabled in YBA. You may enable it "
              + "by setting yb.xcluster.transactional.enabled to true in the application.conf");
    }

    // Check YBDB software version.
    if (!XClusterConfigTaskBase.supportsTxnXCluster(sourceUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Transactional XCluster is not supported in this version of the "
                  + "source universe (%s); please upgrade to a version >= %s",
              sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              XClusterConfigTaskBase.MINIMUN_VERSION_TRANSACTIONAL_XCLUSTER_SUPPORT));
    }
    if (!XClusterConfigTaskBase.supportsTxnXCluster(targetUniverse)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Transactional XCluster is not supported in this version of the "
                  + "target universe (%s); please upgrade to a version >= %s",
              targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              XClusterConfigTaskBase.MINIMUN_VERSION_TRANSACTIONAL_XCLUSTER_SUPPORT));
    }

    // There cannot exist more than one xCluster config when its type is transactional.
    List<XClusterConfig> sourceUniverseXClusterConfigs =
        XClusterConfig.getByUniverseUuid(sourceUniverse.getUniverseUUID());
    List<XClusterConfig> targetUniverseXClusterConfigs =
        XClusterConfig.getByUniverseUuid(targetUniverse.getUniverseUUID());
    if (!sourceUniverseXClusterConfigs.isEmpty() || !targetUniverseXClusterConfigs.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "To create a transactional xCluster, you have to delete all the existing xCluster "
              + "configs on the source and target universes. There could exist at most one "
              + "transactional xCluster config.");
    }

    // Txn xCluster is supported only for YSQL tables.
    if (!tableType.equals(TableType.PGSQL_TABLE_TYPE)) {
      throw new IllegalArgumentException(
          String.format(
              "Transaction xCluster is supported only for YSQL tables. Table type %s is selected",
              tableType));
    }
  }

  public static void xClusterCreatePreChecks(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      ConfigType configType,
      Universe sourceUniverse,
      Universe targetUniverse,
      RuntimeConfGetter confGetter) {
    if (requestedTableInfoList.isEmpty()) {
      throw new IllegalArgumentException("requestedTableInfoList is empty");
    }
    Set<String> tableIds = XClusterConfigTaskBase.getTableIds(requestedTableInfoList);
    CommonTypes.TableType tableType = XClusterConfigTaskBase.getTableType(requestedTableInfoList);

    XClusterConfigTaskBase.verifyTablesNotInReplication(
        tableIds, sourceUniverse.getUniverseUUID(), targetUniverse.getUniverseUUID());
    certsForCdcDirGFlagCheck(sourceUniverse, targetUniverse);

    // XCluster replication can be set up only for YCQL and YSQL tables.
    if (!tableType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)
        && !tableType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)) {
      throw new IllegalArgumentException(
          String.format(
              "XCluster replication can be set up only for YCQL and YSQL tables: "
                  + "type %s requested",
              tableType));
    }

    // There cannot exist more than one xCluster config when there is a txn xCluster config.
    List<XClusterConfig> sourceUniverseXClusterConfigs =
        XClusterConfig.getByUniverseUuid(sourceUniverse.getUniverseUUID());
    List<XClusterConfig> targetUniverseXClusterConfigs =
        XClusterConfig.getByUniverseUuid(targetUniverse.getUniverseUUID());
    if (sourceUniverseXClusterConfigs.stream()
            .anyMatch(xClusterConfig -> xClusterConfig.getType().equals(ConfigType.Txn))
        || targetUniverseXClusterConfigs.stream()
            .anyMatch(xClusterConfig -> xClusterConfig.getType().equals(ConfigType.Txn))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "At least one of the universes has a txn xCluster config. There cannot exist any other "
              + "xCluster config when there is a txn xCluster config.");
    }

    // Make sure only supported relations types are passed in by the user.
    Map<Boolean, List<String>> tableIdsPartitionedByIsXClusterSupported =
        XClusterConfigTaskBase.getTableIdsPartitionedByIsXClusterSupported(requestedTableInfoList);
    if (!tableIdsPartitionedByIsXClusterSupported.get(false).isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Only the following relation types are supported for xCluster replication: %s; The"
                  + " following tables have different relation types or is a colocated child table:"
                  + " %s",
              XClusterConfigTaskBase.X_CLUSTER_SUPPORTED_TABLE_RELATION_TYPE_SET,
              tableIdsPartitionedByIsXClusterSupported.get(false)));
    }

    // TODO: Validate colocated child tables have the same colocation id.

    if (configType.equals(ConfigType.Txn)) {
      XClusterConfigController.transactionalXClusterPreChecks(
          confGetter, sourceUniverse, targetUniverse, tableType);
    }
  }

  public static void xClusterBootstrappingPreChecks(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList,
      Universe targetUniverse,
      Universe sourceUniverse,
      Map<String, String> sourceTableIdTargetTableIdMap,
      YBClientService ybService,
      @Nullable BootstrapParams bootstrapParams,
      @Nullable String currentReplicationGroupName) {

    Set<String> requestedTableIds = XClusterConfigTaskBase.getTableIds(requestedTableInfoList);
    if (bootstrapParams != null && bootstrapParams.tables != null) {
      // Ensure tables in bootstrapParams is a subset of requestedTableIds.
      if (!bootstrapParams.allowBootstrap
          && !requestedTableIds.containsAll(bootstrapParams.tables)) {
        throw new IllegalArgumentException(
            String.format(
                "The set of tables in bootstrapParams (%s) is not a subset of "
                    + "requestedTableIds (%s)",
                bootstrapParams.tables, requestedTableIds));
      }

      // Bootstrapping must not be done for tables whose corresponding target table is in
      // replication. It also includes tables that are in reverse replication between the same
      // universes.
      Map<String, String> sourceTableIdTargetTableIdWithBootstrapMap =
          sourceTableIdTargetTableIdMap.entrySet().stream()
              .filter(entry -> bootstrapParams.tables.contains(entry.getKey()))
              .collect(
                  HashMap::new,
                  (map, entry) -> map.put(entry.getKey(), entry.getValue()),
                  HashMap::putAll);
      if (!bootstrapParams.allowBootstrap) {
        bootstrapParams.tables =
            XClusterConfigTaskBase.getTableIdsWithoutTablesOnTargetInReplication(
                ybService,
                requestedTableInfoList,
                sourceTableIdTargetTableIdWithBootstrapMap,
                targetUniverse,
                currentReplicationGroupName);
      }

      // If some tables do not exist on the target universe, bootstrapping is required.
      Set<String> sourceTableIdsWithNoTableOnTargetUniverse =
          sourceTableIdTargetTableIdMap.entrySet().stream()
              .filter(entry -> Objects.isNull(entry.getValue()))
              .map(Entry::getKey)
              .collect(Collectors.toSet());
      if (!sourceTableIdsWithNoTableOnTargetUniverse.isEmpty()) {
        if (Objects.isNull(bootstrapParams)) {
          throw new IllegalArgumentException(
              String.format(
                  "Table ids %s do not have corresponding tables on the target universe and "
                      + "they must be bootstrapped but bootstrapParams is null",
                  sourceTableIdsWithNoTableOnTargetUniverse));
        }
        if (Objects.isNull(bootstrapParams.tables)
            || !bootstrapParams.tables.containsAll(sourceTableIdsWithNoTableOnTargetUniverse)) {
          throw new IllegalArgumentException(
              String.format(
                  "Table ids %s do not have corresponding tables on the target universe and "
                      + "they must be bootstrapped but the set of tables in bootstrapParams (%s) "
                      + "does not contain all of them",
                  sourceTableIdsWithNoTableOnTargetUniverse, bootstrapParams.tables));
        }
      }

      // If table type is YSQL and bootstrap is requested, all tables in that keyspace are selected.
      if (requestedTableInfoList.get(0).getTableType() == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
        XClusterConfigTaskBase.groupByNamespaceId(requestedTableInfoList)
            .forEach(
                (namespaceId, tablesInfoList) -> {
                  Set<String> selectedTableIdsInNamespaceToBootstrap =
                      XClusterConfigTaskBase.getTableIds(tablesInfoList).stream()
                          .filter(bootstrapParams.tables::contains)
                          .collect(Collectors.toSet());
                  if (!selectedTableIdsInNamespaceToBootstrap.isEmpty()) {
                    Set<String> tableIdsInNamespace =
                        sourceTableInfoList.stream()
                            .filter(
                                tableInfo ->
                                    XClusterConfigTaskBase.isXClusterSupported(tableInfo)
                                        && tableInfo
                                            .getNamespace()
                                            .getId()
                                            .toStringUtf8()
                                            .equals(namespaceId))
                            .map(tableInfo -> tableInfo.getId().toStringUtf8())
                            .collect(Collectors.toSet());
                    if (!bootstrapParams.allowBootstrap
                        && tableIdsInNamespace.size()
                            != selectedTableIdsInNamespaceToBootstrap.size()) {
                      throw new IllegalArgumentException(
                          String.format(
                              "For YSQL tables, all the tables in a keyspace must be selected: "
                                  + "selected: %s, tables in the keyspace: %s",
                              selectedTableIdsInNamespaceToBootstrap, tableIdsInNamespace));
                    }
                  }
                });
      } else if (requestedTableInfoList.get(0).getTableType()
          == CommonTypes.TableType.YQL_TABLE_TYPE) {

        Set<String> mainTableIdsForBootstrap =
            sourceTableInfoList.stream()
                .filter(
                    tableInfo ->
                        bootstrapParams.tables.contains(
                            XClusterConfigTaskBase.getTableId(tableInfo)))
                .filter(tableInfo -> !TableInfoUtil.isIndexTable(tableInfo))
                .map(tableInfo -> XClusterConfigTaskBase.getTableId(tableInfo))
                .collect(Collectors.toSet());

        Map<String, List<String>> mainTableIndexTableInBootstrapMap =
            XClusterConfigTaskBase.getMainTableIndexTablesMap(
                ybService, sourceUniverse, mainTableIdsForBootstrap);
        Set<String> indexTableIdsWithParentTableInBootStrap =
            mainTableIndexTableInBootstrapMap.values().stream()
                .flatMap(List::stream)
                .collect(Collectors.toSet());

        Set<String> indexTablesInBootstrapParamsIds =
            XClusterConfigTaskBase.filterTableInfoListByTableIds(
                    sourceTableInfoList, bootstrapParams.tables)
                .stream()
                .filter(tableInfo -> TableInfoUtil.isIndexTable(tableInfo))
                .map(tableInfo -> XClusterConfigTaskBase.getTableId(tableInfo))
                .collect(Collectors.toSet());

        // Verify that parent table is also added to bootstrap tables list if index table require
        // bootstrap.
        indexTablesInBootstrapParamsIds.forEach(
            indexTableId -> {
              if (!indexTableIdsWithParentTableInBootStrap.contains(indexTableId)) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format(
                        "The index table %s which require bootstrap does not have main table in"
                            + " bootstrap tables list",
                        indexTableId));
              }
            });
      }
    }
  }

  /**
   * This method retrieves all the tables required for bootstrapping in a cross-cluster setup. It
   * first checks the type of the tables in the replication info list. If the table type is
   * PGSQL_TABLE_TYPE, it groups all tables by their namespace ID and adds all table IDs in each
   * namespace to the bootstrapping list. For YCQL tables, we add all tables in the replication info
   * list to the bootstrapping list.
   *
   * @param ybService The YB client service.
   * @param tablesInReplicationInfoList A list of all tables in the replication info list.
   * @param tablesInReplication A set of table IDs that are already in replication.
   * @param sourceUniverse The source universe of the cross-cluster setup.
   * @return A set of table IDs that are required for bootstrapping.
   */
  public static Set<String> getAllBootstrapRequiredTableForXClusterRequestedTable(
      YBClientService ybService,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInReplicationInfoList,
      Set<String> tablesInReplication,
      Universe sourceUniverse) {

    Set<String> tableIdsForBootstrap = new HashSet<>(tablesInReplication);
    CommonTypes.TableType tableType = tablesInReplicationInfoList.get(0).getTableType();
    if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      XClusterConfigTaskBase.groupByNamespaceId(tablesInReplicationInfoList)
          .forEach(
              (namespaceId, tablesInfoList) -> {
                List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> namespaceTables =
                    XClusterConfigTaskBase.getTableInfoListByNamespaceId(
                        ybService, sourceUniverse, tableType, namespaceId);
                namespaceTables.stream()
                    .map(tableInfo -> XClusterConfigTaskBase.getTableId(tableInfo))
                    .forEach(tableIdsForBootstrap::add);
              });
    }
    return tableIdsForBootstrap;
  }
}
