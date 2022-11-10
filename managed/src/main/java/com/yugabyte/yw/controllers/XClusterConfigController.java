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
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigGetResp;
import com.yugabyte.yw.forms.XClusterConfigNeedBootstrapFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
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
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Asynchronous Replication",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class XClusterConfigController extends AuthenticatedController {

  private final Commissioner commissioner;
  private final MetricQueryHelper metricQueryHelper;
  private final BackupUtil backupUtil;
  private final CustomerConfigService customerConfigService;
  private final YBClientService ybService;

  @Inject
  public XClusterConfigController(
      Commissioner commissioner,
      MetricQueryHelper metricQueryHelper,
      BackupUtil backupUtil,
      CustomerConfigService customerConfigService,
      YBClientService ybService) {
    this.commissioner = commissioner;
    this.metricQueryHelper = metricQueryHelper;
    this.backupUtil = backupUtil;
    this.customerConfigService = customerConfigService;
    this.ybService = ybService;
  }

  /**
   * API that creates an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "createXClusterConfig",
      value = "Create xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_create_form_data",
          value = "XCluster Replication Create Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigCreateFormData",
          paramType = "body",
          required = true))
  public Result create(UUID customerUUID) {
    log.info("Received create XClusterConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigCreateFormData createFormData = parseCreateFormData(customerUUID);
    Universe sourceUniverse =
        Universe.getValidUniverseOrBadRequest(createFormData.sourceUniverseUUID, customer);
    Universe targetUniverse =
        Universe.getValidUniverseOrBadRequest(createFormData.targetUniverseUUID, customer);
    checkConfigDoesNotAlreadyExist(
        createFormData.name, createFormData.sourceUniverseUUID, createFormData.targetUniverseUUID);
    verifyTablesNotInReplication(
        createFormData.tables,
        createFormData.sourceUniverseUUID,
        createFormData.targetUniverseUUID);

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

    // Add index tables.
    Map<String, List<String>> mainTableIndexTablesMap =
        XClusterConfigTaskBase.getMainTableIndexTablesMap(
            this.ybService, sourceUniverse, createFormData.tables);
    Set<String> indexTableIdSet =
        mainTableIndexTablesMap.values().stream().flatMap(List::stream).collect(Collectors.toSet());
    createFormData.tables.addAll(indexTableIdSet);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
            this.ybService,
            createFormData.tables,
            createFormData.bootstrapParams,
            sourceUniverse,
            targetUniverse);

    if (createFormData.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    }

    // Create xCluster config object.
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, requestedTableInfoList, indexTableIdSet);
    verifyTaskAllowed(xClusterConfig, TaskType.CreateXClusterConfig);

    // Submit task to set up xCluster config.
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(
            xClusterConfig,
            createFormData.bootstrapParams,
            requestedTableInfoList,
            mainTableIndexTablesMap);
    UUID taskUUID = commissioner.submit(TaskType.CreateXClusterConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.universeUUID,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.Create,
        xClusterConfig.name);

    log.info("Submitted create XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.XClusterConfig,
            Objects.toString(xClusterConfig.uuid, null),
            Audit.ActionType.Create,
            Json.toJson(createFormData),
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.uuid).asResult();
  }

  /**
   * API that gets an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "getXClusterConfig",
      value = "Get xcluster config",
      response = XClusterConfigGetResp.class)
  public Result get(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received get XClusterConfig({}) request", xclusterConfigUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);

    JsonNode lagMetricData;

    try {
      Set<String> streamIds = xClusterConfig.getStreamIdsWithReplicationSetup();
      log.info(
          "Querying lag metrics for XClusterConfig({}) using CDC stream IDs: {}",
          xClusterConfig.uuid,
          streamIds);

      // Query for replication lag
      Map<String, String> metricParams = new HashMap<>();
      String metric = "tserver_async_replication_lag_micros";
      metricParams.put("metrics[0]", metric);
      String startTime = Long.toString(Instant.now().minus(Duration.ofMinutes(1)).getEpochSecond());
      metricParams.put("start", startTime);
      ObjectNode filterJson = Json.newObject();
      Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
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
              xClusterConfig.uuid, e.getMessage());
      log.error(errorMsg);
      lagMetricData = Json.newObject().put("error", errorMsg);
    }

    // Check whether the replication is broken for the tables.
    Set<String> tableIdsInRunningStatus =
        xClusterConfig.getTableIdsInStatus(
            xClusterConfig.getTables(), XClusterTableConfig.Status.Running);
    Map<String, Boolean> isBootstrapRequiredMap;
    try {
      isBootstrapRequiredMap =
          XClusterConfigTaskBase.isBootstrapRequired(
              this.ybService, tableIdsInRunningStatus, xClusterConfig);
    } catch (Exception e) {
      log.error("XClusterConfigTaskBase.isBootstrapRequired hit error : {}", e.getMessage());
      // If isBootstrapRequired method hits error, assume all the tables are in error state.
      isBootstrapRequiredMap =
          tableIdsInRunningStatus
              .stream()
              .collect(Collectors.toMap(tableId -> tableId, tableId -> true));
    }
    Set<String> tableIdsInErrorStatus =
        isBootstrapRequiredMap
            .entrySet()
            .stream()
            .filter(Map.Entry::getValue)
            .map(Map.Entry::getKey)
            .collect(Collectors.toSet());
    xClusterConfig
        .getTableDetails()
        .stream()
        .filter(tableConfig -> tableIdsInErrorStatus.contains(tableConfig.tableId))
        .forEach(tableConfig -> tableConfig.status = XClusterTableConfig.Status.Error);

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
      value = "Edit xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_edit_form_data",
          value = "XCluster Replication Edit Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigEditFormData",
          paramType = "body",
          required = true))
  public Result edit(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received edit XClusterConfig({}) request", xclusterConfigUUID);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigEditFormData editFormData = parseEditFormData(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);
    verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);
    Universe sourceUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.sourceUniverseUUID, customer);
    Universe targetUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.targetUniverseUUID, customer);

    Map<String, List<String>> mainTableToAddIndexTablesMap = null;
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList = null;
    Set<String> tableIdsToAdd = null;
    Set<String> tableIdsToRemove = null;
    if (editFormData.tables != null) {
      Set<String> currentTableIds = xClusterConfig.getTables();
      Pair<Set<String>, Set<String>> tableIdsToAddTableIdsToRemovePair =
          XClusterConfigTaskBase.getTableIdsDiff(currentTableIds, editFormData.tables);
      tableIdsToAdd = tableIdsToAddTableIdsToRemovePair.getFirst();
      tableIdsToRemove = tableIdsToAddTableIdsToRemovePair.getSecond();
      log.info("tableIdsToAdd are {}; tableIdsToRemove are {}", tableIdsToAdd, tableIdsToRemove);

      // For backward compatibility; if table is in replication, no need fot bootstrapping.
      xClusterConfig.setNeedBootstrapForTables(
          xClusterConfig.getTableIdsWithReplicationSetup(), false /* needBootstrap */);

      if (!tableIdsToAdd.isEmpty()) {
        // Add index tables.
        Set<String> allTableIds =
            editFormData.bootstrapParams == null
                ? tableIdsToAdd
                : Stream.concat(
                        tableIdsToAdd.stream(), editFormData.bootstrapParams.tables.stream())
                    .collect(Collectors.toSet());
        mainTableToAddIndexTablesMap =
            XClusterConfigTaskBase.getMainTableIndexTablesMap(
                this.ybService, sourceUniverse, allTableIds);
        Set<String> indexTableIdSet =
            mainTableToAddIndexTablesMap
                .values()
                .stream()
                .flatMap(List::stream)
                .collect(Collectors.toSet());
        Set<String> indexTableIdSetToAdd =
            indexTableIdSet
                .stream()
                .filter(tableId -> !xClusterConfig.getTables().contains(tableId))
                .collect(Collectors.toSet());
        allTableIds.addAll(indexTableIdSet);
        tableIdsToAdd.addAll(indexTableIdSetToAdd);

        verifyTablesNotInReplication(
            tableIdsToAdd, xClusterConfig.sourceUniverseUUID, xClusterConfig.targetUniverseUUID);

        requestedTableToAddInfoList =
            XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                this.ybService,
                allTableIds,
                editFormData.bootstrapParams,
                sourceUniverse,
                targetUniverse,
                xClusterConfig.getReplicationGroupName());

        CommonTypes.TableType tableType = requestedTableToAddInfoList.get(0).getTableType();
        if (!xClusterConfig.tableType.equals(XClusterConfig.TableType.UNKNOWN)) {
          if (!xClusterConfig.getTableTypeAsCommonType().equals(tableType)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "The xCluster config has a type of %s, but the tables to be added have a "
                        + "type of %s",
                    xClusterConfig.tableType,
                    XClusterConfig.XClusterConfigTableTypeCommonTypesTableTypeBiMap.inverse()
                        .get(tableType)));
          }
        }

        if (!editFormData.dryRun) {
          // Save the to-be-added tables in the DB.
          xClusterConfig.addTablesIfNotExist(tableIdsToAdd, editFormData.bootstrapParams);
          xClusterConfig.setIndexTableForTables(indexTableIdSetToAdd, true /* indexTable */);
        }
      }

      if (!tableIdsToRemove.isEmpty()) {
        // Remove index tables if its main table is removed.
        Map<String, List<String>> mainTableIndexTablesMap =
            XClusterConfigTaskBase.getMainTableIndexTablesMap(
                this.ybService, sourceUniverse, tableIdsToRemove);
        Set<String> indexTableIdSet =
            mainTableIndexTablesMap
                .values()
                .stream()
                .flatMap(List::stream)
                .filter(currentTableIds::contains)
                .collect(Collectors.toSet());
        tableIdsToRemove.addAll(indexTableIdSet);
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
    }

    // If renaming, verify xCluster replication with same name (between same source/target)
    // does not already exist.
    if (editFormData.name != null) {
      if (XClusterConfig.getByNameSourceTarget(
              editFormData.name,
              xClusterConfig.sourceUniverseUUID,
              xClusterConfig.targetUniverseUUID)
          != null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "XClusterConfig with same name already exists");
      }
    }

    if (editFormData.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    }

    // Submit task to edit xCluster config.
    XClusterConfigTaskParams params =
        new XClusterConfigTaskParams(
            xClusterConfig,
            editFormData,
            requestedTableToAddInfoList,
            mainTableToAddIndexTablesMap,
            tableIdsToAdd,
            tableIdsToRemove);
    UUID taskUUID = commissioner.submit(TaskType.EditXClusterConfig, params);
    CustomerTask.create(
        customer,
        xClusterConfig.sourceUniverseUUID,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.Edit,
        xClusterConfig.name);

    log.info("Submitted edit XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.XClusterConfig,
            xclusterConfigUUID.toString(),
            Audit.ActionType.Edit,
            Json.toJson(editFormData),
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.uuid).asResult();
  }

  /**
   * API that restarts an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "restartXClusterConfig",
      value = "Restart xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_restart_form_data",
          value = "XCluster Replication Restart Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigRestartFormData",
          paramType = "body",
          required = true))
  public Result restart(UUID customerUUID, UUID xClusterConfigUUID, boolean isForceDelete) {
    log.info(
        "Received restart XClusterConfig({}) request with isForceDelete={}",
        xClusterConfigUUID,
        isForceDelete);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xClusterConfigUUID);
    XClusterConfigRestartFormData restartFormData =
        parseRestartFormData(customerUUID, xClusterConfig);
    verifyTaskAllowed(xClusterConfig, TaskType.RestartXClusterConfig);
    Universe sourceUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.sourceUniverseUUID, customer);
    Universe targetUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.targetUniverseUUID, customer);

    Set<String> tableIds = restartFormData.tables;
    // Add index tables.
    Map<String, List<String>> mainTableIndexTablesMap =
        XClusterConfigTaskBase.getMainTableIndexTablesMap(this.ybService, sourceUniverse, tableIds);
    Set<String> indexTableIdSet =
        mainTableIndexTablesMap.values().stream().flatMap(List::stream).collect(Collectors.toSet());
    tableIds.addAll(indexTableIdSet);
    if (!restartFormData.dryRun) {
      xClusterConfig.addTablesIfNotExist(
          indexTableIdSet, null /* tableIdsNeedBootstrap */, true /* areIndexTables */);
    }

    XClusterConfigCreateFormData.BootstrapParams bootstrapParams = null;
    if (restartFormData.bootstrapParams != null) {
      bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
      bootstrapParams.backupRequestParams = restartFormData.bootstrapParams.backupRequestParams;
      bootstrapParams.tables = tableIds;
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
            this.ybService,
            tableIds,
            bootstrapParams,
            sourceUniverse,
            targetUniverse,
            xClusterConfig.getReplicationGroupName());

    if (restartFormData.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    } else {
      // Set table type for old xCluster configs.
      xClusterConfig.setTableType(requestedTableInfoList);
      if (bootstrapParams != null) {
        // Set needBootstrap to true for all tables. It will check if it is required.
        xClusterConfig.setNeedBootstrapForTables(bootstrapParams.tables, true /* needBootstrap */);
      }
    }

    // Submit task to edit xCluster config.
    XClusterConfigTaskParams params =
        new XClusterConfigTaskParams(
            xClusterConfig,
            bootstrapParams,
            requestedTableInfoList,
            mainTableIndexTablesMap,
            isForceDelete);
    UUID taskUUID = commissioner.submit(TaskType.RestartXClusterConfig, params);
    CustomerTask.create(
        customer,
        xClusterConfig.sourceUniverseUUID,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.Restart,
        xClusterConfig.name);

    log.info("Submitted restart XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.XClusterConfig,
            xClusterConfigUUID.toString(),
            Audit.ActionType.Restart,
            taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.uuid).asResult();
  }

  /**
   * API that deletes an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "deleteXClusterConfig",
      value = "Delete xcluster config",
      response = YBPTask.class)
  public Result delete(UUID customerUUID, UUID xClusterConfigUuid, boolean isForceDelete) {
    log.info(
        "Received delete XClusterConfig({}) request with isForceDelete={}",
        xClusterConfigUuid,
        isForceDelete);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xClusterConfigUuid);
    verifyTaskAllowed(xClusterConfig, TaskType.DeleteXClusterConfig);

    Universe sourceUniverse = null;
    Universe targetUniverse = null;
    if (xClusterConfig.sourceUniverseUUID != null) {
      sourceUniverse =
          Universe.getValidUniverseOrBadRequest(xClusterConfig.sourceUniverseUUID, customer);
    }
    if (xClusterConfig.targetUniverseUUID != null) {
      targetUniverse =
          Universe.getValidUniverseOrBadRequest(xClusterConfig.targetUniverseUUID, customer);
    }

    // Submit task to delete xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig, isForceDelete);
    UUID taskUUID = commissioner.submit(TaskType.DeleteXClusterConfig, params);
    if (sourceUniverse != null) {
      CustomerTask.create(
          customer,
          sourceUniverse.universeUUID,
          taskUUID,
          CustomerTask.TargetType.XClusterConfig,
          CustomerTask.TaskType.Delete,
          xClusterConfig.name);
    } else if (targetUniverse != null) {
      CustomerTask.create(
          customer,
          targetUniverse.universeUUID,
          taskUUID,
          CustomerTask.TargetType.XClusterConfig,
          CustomerTask.TaskType.Delete,
          xClusterConfig.name);
    }
    log.info("Submitted delete XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
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
      value = "Sync xcluster config",
      response = YBPTask.class)
  public Result sync(UUID customerUUID, UUID targetUniverseUUID) {
    log.info("Received sync XClusterConfig request for universe({})", targetUniverseUUID);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe targetUniverse = Universe.getValidUniverseOrBadRequest(targetUniverseUUID, customer);

    // Submit task to sync xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(targetUniverseUUID);
    UUID taskUUID = commissioner.submit(TaskType.SyncXClusterConfig, params);
    CustomerTask.create(
        customer,
        targetUniverseUUID,
        taskUUID,
        TargetType.XClusterConfig,
        CustomerTask.TaskType.Sync,
        targetUniverse.name);

    log.info(
        "Submitted sync XClusterConfig for universe({}), task {}", targetUniverseUUID, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            targetUniverseUUID.toString(),
            Audit.ActionType.SyncXClusterConfig,
            taskUUID);
    return new YBPTask(taskUUID).asResult();
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
      value = "Whether tables need bootstrap before setting up cross cluster replication",
      response = Map.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_need_bootstrap_form_data",
          value = "XCluster Need Bootstrap Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigNeedBootstrapFormData",
          paramType = "body",
          required = true))
  public Result needBootstrapTable(UUID customerUuid, UUID sourceUniverseUuid) {
    log.info("Received needBootstrapTable request for sourceUniverseUuid={}", sourceUniverseUuid);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUuid);
    XClusterConfigNeedBootstrapFormData needBootstrapFormData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigNeedBootstrapFormData.class);
    Universe.getValidUniverseOrBadRequest(sourceUniverseUuid, customer);
    needBootstrapFormData.tables =
        XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(needBootstrapFormData.tables);

    try {
      Map<String, Boolean> isBootstrapRequiredMap =
          XClusterConfigTaskBase.isBootstrapRequired(
              ybService, needBootstrapFormData.tables, sourceUniverseUuid);
      return PlatformResults.withData(isBootstrapRequiredMap);
    } catch (Exception e) {
      log.error("XClusterConfigTaskBase.isBootstrapRequired hit error : {}", e.getMessage());
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
      value =
          "Whether tables in an xCluster replication config have fallen far behind and need "
              + "bootstrap",
      response = Map.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_need_bootstrap_form_data",
          value = "XCluster Need Bootstrap Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigNeedBootstrapFormData",
          paramType = "body",
          required = true))
  public Result needBootstrap(UUID customerUuid, UUID xClusterConfigUuid) {
    log.info("Received needBootstrap request for xClusterConfigUuid={}", xClusterConfigUuid);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUuid);
    XClusterConfigNeedBootstrapFormData needBootstrapFormData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigNeedBootstrapFormData.class);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xClusterConfigUuid);
    needBootstrapFormData.tables =
        XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(needBootstrapFormData.tables);

    try {
      Map<String, Boolean> isBootstrapRequiredMap =
          XClusterConfigTaskBase.isBootstrapRequired(
              ybService, needBootstrapFormData.tables, xClusterConfig);
      return PlatformResults.withData(isBootstrapRequiredMap);
    } catch (Exception e) {
      log.error("XClusterConfigTaskBase.isBootstrapRequired hit error : {}", e.getMessage());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Exception happened while running IsBootstrapRequired API: %s", e.getMessage()));
    }
  }

  private XClusterConfigCreateFormData parseCreateFormData(UUID customerUUID) {
    log.debug("Request body to create an xCluster config is {}", request().body().asJson());
    XClusterConfigCreateFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigCreateFormData.class);

    if (Objects.equals(formData.sourceUniverseUUID, formData.targetUniverseUUID)) {
      throw new IllegalArgumentException(
          String.format(
              "Source and target universe cannot be the same: both are %s",
              formData.sourceUniverseUUID));
    }

    formData.tables = XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(formData.tables);

    // Validate bootstrap parameters if there is any.
    if (formData.bootstrapParams != null) {
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams = formData.bootstrapParams;
      bootstrapParams.tables =
          XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(bootstrapParams.tables);
      // Ensure tables in BootstrapParams is a subset of tables in the main body.
      if (!formData.tables.containsAll(bootstrapParams.tables)) {
        throw new IllegalArgumentException(
            String.format(
                "The set of tables in bootstrapParams (%s) is not a subset of tables in the "
                    + "main body (%s)",
                bootstrapParams.tables, formData.tables));
      }

      // Fail early if parameters are invalid for bootstrapping.
      if (bootstrapParams.tables.size() > 0) {
        validateBackupRequestParamsForBootstrapping(
            bootstrapParams.backupRequestParams, customerUUID);
      }
    }

    return formData;
  }

  private XClusterConfigEditFormData parseEditFormData(UUID customerUUID) {
    log.debug("Request body to edit an xCluster config is {}", request().body().asJson());
    XClusterConfigEditFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigEditFormData.class);

    // Ensure exactly one edit form field is specified
    int numEditOps = 0;
    numEditOps += (formData.name != null) ? 1 : 0;
    numEditOps += (formData.status != null) ? 1 : 0;
    numEditOps += (formData.tables != null && !formData.tables.isEmpty()) ? 1 : 0;
    if (numEditOps == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Must specify an edit operation");
    } else if (numEditOps > 1) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Exactly one edit request (either editName, editStatus, editTables) is allowed in "
              + "one call.");
    }

    if (formData.tables != null && !formData.tables.isEmpty()) {
      formData.tables = XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(formData.tables);
      // Validate bootstrap parameters if there is any.
      if (formData.bootstrapParams != null) {
        XClusterConfigCreateFormData.BootstrapParams bootstrapParams = formData.bootstrapParams;
        bootstrapParams.tables =
            XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(bootstrapParams.tables);
        // Ensure tables in BootstrapParams is a subset of tables in the main body.
        if (!formData.tables.containsAll(bootstrapParams.tables)) {
          throw new IllegalArgumentException(
              String.format(
                  "The set of tables in bootstrapParams (%s) is not a subset of tables in the "
                      + "main body (%s)",
                  bootstrapParams.tables, formData.tables));
        }

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
      UUID customerUUID, XClusterConfig xClusterConfig) {
    log.debug("Request body to restart an xCluster config is {}", request().body().asJson());
    XClusterConfigRestartFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigRestartFormData.class);

    formData.tables = XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(formData.tables);

    if (formData.bootstrapParams != null) {
      validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);
    }

    Set<String> tableIds = xClusterConfig.getTables();
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

      if (xClusterConfig.status == XClusterConfig.XClusterConfigStatusType.Failed
          && formData.tables.size() != tableIds.size()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Per DB/table xCluster config restart cannot be done because the creation of the "
                + "xCluster config failed; please do not specify the `tables` field so the whole "
                + "xCluster config restarts");
      }
    } else {
      formData.tables = tableIds;
    }

    return formData;
  }

  private void checkConfigDoesNotAlreadyExist(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    XClusterConfig xClusterConfig =
        XClusterConfig.getByNameSourceTarget(name, sourceUniverseUUID, targetUniverseUUID);

    if (xClusterConfig != null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "xCluster config between source universe "
              + sourceUniverseUUID
              + " and target universe "
              + targetUniverseUUID
              + " with name '"
              + name
              + "' already exists.");
    }
  }

  private void verifyTaskAllowed(XClusterConfig xClusterConfig, TaskType taskType) {
    if (!XClusterConfigTaskBase.isTaskAllowed(xClusterConfig, taskType)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s task is not allowed; with status `%s`, the allowed tasks are %s",
              taskType,
              xClusterConfig.status,
              XClusterConfigTaskBase.getAllowedTasks(xClusterConfig)));
    }
  }

  /**
   * It ensures that none of the tables specified at parameter {@code tables} is in replication
   * between two universes in more than one xCluster config.
   *
   * @param tableIds The set of tables that must not be already in replication between the same
   *     universe in the same direction
   * @param sourceUniverseUUID The source universe uuid
   * @param targetUniverseUUID The target universe uuid
   */
  private void verifyTablesNotInReplication(
      Set<String> tableIds, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getBetweenUniverses(sourceUniverseUUID, targetUniverseUUID);
    xClusterConfigs.forEach(
        config -> {
          Set<String> tablesInReplication = config.getTables();
          tablesInReplication.retainAll(tableIds);
          if (!tablesInReplication.isEmpty()) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Table(s) with ID %s are already in replication between these universes in "
                        + "the same direction",
                    tablesInReplication));
          }
        });
  }

  private void validateBackupRequestParamsForBootstrapping(
      XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams bootstarpBackupParams,
      UUID customerUUID) {
    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(
            customerUUID, bootstarpBackupParams.storageConfigUUID);
    if (!customerConfig.getState().equals(CustomerConfig.ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot create backup as config is queued for deletion.");
    }
    backupUtil.validateStorageConfig(customerConfig);
  }
}
