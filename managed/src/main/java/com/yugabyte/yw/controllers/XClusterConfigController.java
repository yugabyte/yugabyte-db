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
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigGetResp;
import com.yugabyte.yw.forms.XClusterConfigNeedBootstrapFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
import com.yugabyte.yw.forms.XClusterConfigSyncFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
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
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
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
  private final BackupUtil backupUtil;
  private final CustomerConfigService customerConfigService;
  private final YBClientService ybService;
  private final RuntimeConfGetter confGetter;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  public XClusterConfigController(
      Commissioner commissioner,
      MetricQueryHelper metricQueryHelper,
      BackupUtil backupUtil,
      CustomerConfigService customerConfigService,
      YBClientService ybService,
      RuntimeConfGetter confGetter,
      XClusterUniverseService xClusterUniverseService) {
    this.commissioner = commissioner;
    this.metricQueryHelper = metricQueryHelper;
    this.backupUtil = backupUtil;
    this.customerConfigService = customerConfigService;
    this.ybService = ybService;
    this.confGetter = confGetter;
    this.xClusterUniverseService = xClusterUniverseService;
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
  public Result create(UUID customerUUID, Http.Request request) {
    log.info("Received create XClusterConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigCreateFormData createFormData = parseCreateFormData(customerUUID, request);
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

    if (createFormData.configType.equals(ConfigType.Txn)) {
      if (!confGetter.getGlobalConf(GlobalConfKeys.transactionalXClusterEnabled)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Support for transactional xCluster configs is disabled in YBA. You may enable it "
                + "by setting xcluster.transactional.enabled to true in the application.conf");
      }

      // Check YBDB software version.
      if (!XClusterConfigTaskBase.supportsTxnXCluster(sourceUniverse)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Transactional XCluster is not supported in this version of the "
                    + "source universe (%s); please upgrade to a version >= %s",
                sourceUniverse
                    .getUniverseDetails()
                    .getPrimaryCluster()
                    .userIntent
                    .ybSoftwareVersion,
                XClusterConfigTaskBase.MINIMUN_VERSION_TRANSACTIONAL_XCLUSTER_SUPPORT));
      }
      if (!XClusterConfigTaskBase.supportsTxnXCluster(targetUniverse)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Transactional XCluster is not supported in this version of the "
                    + "target universe (%s); please upgrade to a version >= %s",
                targetUniverse
                    .getUniverseDetails()
                    .getPrimaryCluster()
                    .userIntent
                    .ybSoftwareVersion,
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
      // Savepoints must be disabled for txn xCluster.
      Cluster sourceUniversePrimaryCluster =
          sourceUniverse.getUniverseDetails().getPrimaryCluster();
      Cluster targetUniversePrimaryCluster =
          targetUniverse.getUniverseDetails().getPrimaryCluster();
      if (!Objects.equals(
              sourceUniversePrimaryCluster.userIntent.masterGFlags.get(
                  XClusterConfigTaskBase.ENABLE_PG_SAVEPOINTS_GFLAG_NAME),
              "false")
          || !Objects.equals(
              sourceUniversePrimaryCluster.userIntent.tserverGFlags.get(
                  XClusterConfigTaskBase.ENABLE_PG_SAVEPOINTS_GFLAG_NAME),
              "false")
          || !Objects.equals(
              targetUniversePrimaryCluster.userIntent.masterGFlags.get(
                  XClusterConfigTaskBase.ENABLE_PG_SAVEPOINTS_GFLAG_NAME),
              "false")
          || !Objects.equals(
              targetUniversePrimaryCluster.userIntent.tserverGFlags.get(
                  XClusterConfigTaskBase.ENABLE_PG_SAVEPOINTS_GFLAG_NAME),
              "false")) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "To create a transactional xCluster, you have set %s to `false` on both "
                    + "source and target universes",
                XClusterConfigTaskBase.ENABLE_PG_SAVEPOINTS_GFLAG_NAME));
      }
    }

    // Add index tables.
    Map<String, List<String>> mainTableIndexTablesMap =
        XClusterConfigTaskBase.getMainTableIndexTablesMap(
            this.ybService, sourceUniverse, createFormData.tables);
    Set<String> indexTableIdSet =
        mainTableIndexTablesMap.values().stream().flatMap(List::stream).collect(Collectors.toSet());
    createFormData.tables.addAll(indexTableIdSet);

    Pair<List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>, Set<String>>
        requestedTableInfoList_sourceTableIdsWithNoTableOnTargetUniverse =
            XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                this.ybService,
                createFormData.tables,
                createFormData.bootstrapParams,
                sourceUniverse,
                targetUniverse,
                null /* currentReplicationGroupName */,
                createFormData.configType);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        requestedTableInfoList_sourceTableIdsWithNoTableOnTargetUniverse.getFirst();
    Set<String> sourceTableIdsWithNoTableOnTargetUniverse =
        requestedTableInfoList_sourceTableIdsWithNoTableOnTargetUniverse.getSecond();

    // PITR must be configured for the DBs in case of txn.
    if (createFormData.configType.equals(ConfigType.Txn)) {
      Set<String> dbNamesWithoutPitr = new HashSet<>();
      CommonTypes.TableType tableType = requestedTableInfoList.get(0).getTableType();
      XClusterConfigTaskBase.groupByNamespaceName(requestedTableInfoList)
          .forEach(
              (dbName, tableInfoList) -> {
                if (!PitrConfig.maybeGet(targetUniverse.getUniverseUUID(), tableType, dbName)
                    .isPresent()) {
                  dbNamesWithoutPitr.add(dbName);
                }
              });
      if (!dbNamesWithoutPitr.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "To create a transactional xCluster, you have to configure PITR for the DBs on "
                    + "the target universe. DBs wihtout PITR configured are %s",
                dbNamesWithoutPitr));
      }
    }

    if (createFormData.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    }

    // Create xCluster config object.
    XClusterConfig xClusterConfig = XClusterConfig.create(createFormData, requestedTableInfoList);
    verifyTaskAllowed(xClusterConfig, TaskType.CreateXClusterConfig);

    // Submit task to set up xCluster config.
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(
            xClusterConfig,
            createFormData.bootstrapParams,
            requestedTableInfoList,
            mainTableIndexTablesMap,
            sourceTableIdsWithNoTableOnTargetUniverse);
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
          xClusterConfig.getUuid(),
          streamIds);

      // Query for replication lag
      Map<String, String> metricParams = new HashMap<>();
      String metric = "tserver_async_replication_lag_micros";
      metricParams.put("metrics[0]", metric);
      String startTime = Long.toString(Instant.now().minus(Duration.ofMinutes(1)).getEpochSecond());
      metricParams.put("start", startTime);
      ObjectNode filterJson = Json.newObject();
      Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
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

    // Check whether the replication is broken for the tables.
    Set<String> tableIdsInRunningStatus =
        xClusterConfig.getTableIdsInStatus(
            xClusterConfig.getTableIds(), XClusterTableConfig.Status.Running);
    try {
      Map<String, Boolean> isBootstrapRequiredMap =
          this.xClusterUniverseService.isBootstrapRequired(
              tableIdsInRunningStatus,
              xClusterConfig,
              xClusterConfig.getSourceUniverseUUID(),
              true /* ignoreErrors */);

      // If IsBootstrapRequired API returns null, set the statuses to UnableToFetch.
      if (Objects.isNull(isBootstrapRequiredMap)) {
        // We do not update the xCluster config object in the DB intentionally because
        // `UnableToFetch` is only a user facing status.
        xClusterConfig.getTableDetails().stream()
            .filter(tableConfig -> tableIdsInRunningStatus.contains(tableConfig.getTableId()))
            .forEach(
                tableConfig -> tableConfig.setStatus(XClusterTableConfig.Status.UnableToFetch));
      } else {
        Set<String> tableIdsInErrorStatus =
            isBootstrapRequiredMap.entrySet().stream()
                .filter(Entry::getValue)
                .map(Map.Entry::getKey)
                .collect(Collectors.toSet());
        // We do not update the xCluster config object in the DB intentionally because `Error` is
        // only a user facing status.
        xClusterConfig.getTableDetails().stream()
            .filter(tableConfig -> tableIdsInErrorStatus.contains(tableConfig.getTableId()))
            .forEach(tableConfig -> tableConfig.setStatus(XClusterTableConfig.Status.Error));

        // Set the status for the rest of tables where isBootstrapRequired RPC failed.
        xClusterConfig.getTableDetails().stream()
            .filter(
                tableConfig ->
                    tableIdsInRunningStatus.contains(tableConfig.getTableId())
                        && !isBootstrapRequiredMap.containsKey(tableConfig.getTableId()))
            .forEach(
                tableConfig -> tableConfig.setStatus(XClusterTableConfig.Status.UnableToFetch));
      }
    } catch (Exception e) {
      log.error("XClusterConfigTaskBase.isBootstrapRequired hit error : {}", e.getMessage());
    }

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
  public Result edit(UUID customerUUID, UUID xclusterConfigUUID, Http.Request request) {
    log.info("Received edit XClusterConfig({}) request", xclusterConfigUUID);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigEditFormData editFormData = parseEditFormData(customerUUID, request);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);
    verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);
    Universe sourceUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    Map<String, List<String>> mainTableToAddIndexTablesMap = null;
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableToAddInfoList = null;
    Set<String> tableIdsToAdd = null;
    Set<String> tableIdsToRemove = null;
    if (editFormData.tables != null) {
      Set<String> currentTableIds = xClusterConfig.getTableIds();
      Pair<Set<String>, Set<String>> tableIdsToAddTableIdsToRemovePair =
          XClusterConfigTaskBase.getTableIdsDiff(currentTableIds, editFormData.tables);
      tableIdsToAdd = tableIdsToAddTableIdsToRemovePair.getFirst();
      tableIdsToRemove = tableIdsToAddTableIdsToRemovePair.getSecond();
      log.info("tableIdsToAdd are {}; tableIdsToRemove are {}", tableIdsToAdd, tableIdsToRemove);

      // For backward compatibility; if table is in replication, no need fot bootstrapping.
      xClusterConfig.updateNeedBootstrapForTables(
          xClusterConfig.getTableIdsWithReplicationSetup(), false /* needBootstrap */);

      if (!tableIdsToAdd.isEmpty()) {
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
            mainTableToAddIndexTablesMap.values().stream()
                .flatMap(List::stream)
                .collect(Collectors.toSet());
        Set<String> indexTableIdSetToAdd =
            indexTableIdSet.stream()
                .filter(tableId -> !xClusterConfig.getTableIds().contains(tableId))
                .collect(Collectors.toSet());
        allTableIds.addAll(indexTableIdSet);
        tableIdsToAdd.addAll(indexTableIdSetToAdd);

        verifyTablesNotInReplication(
            tableIdsToAdd,
            xClusterConfig.getSourceUniverseUUID(),
            xClusterConfig.getTargetUniverseUUID());

        Pair<List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>, Set<String>>
            requestedTableInfoList_sourceTableIdsWithNoTableOnTargetUniverse =
                XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                    this.ybService,
                    allTableIds,
                    editFormData.bootstrapParams,
                    sourceUniverse,
                    targetUniverse,
                    xClusterConfig.getReplicationGroupName(),
                    xClusterConfig.getType());
        requestedTableToAddInfoList =
            requestedTableInfoList_sourceTableIdsWithNoTableOnTargetUniverse.getFirst();

        CommonTypes.TableType tableType = requestedTableToAddInfoList.get(0).getTableType();
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

        if (!editFormData.dryRun) {
          // Save the to-be-added tables in the DB.
          xClusterConfig.addTablesIfNotExist(tableIdsToAdd, editFormData.bootstrapParams);
          xClusterConfig.updateIndexTableForTables(indexTableIdSetToAdd, true /* indexTable */);
        }
      }

      if (!tableIdsToRemove.isEmpty()) {
        // Remove index tables if its main table is removed.
        Map<String, List<String>> mainTableIndexTablesMap =
            XClusterConfigTaskBase.getMainTableIndexTablesMap(
                this.ybService, sourceUniverse, tableIdsToRemove);
        Set<String> indexTableIdSet =
            mainTableIndexTablesMap.values().stream()
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
              xClusterConfig.getSourceUniverseUUID(),
              xClusterConfig.getTargetUniverseUUID())
          != null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "XClusterConfig with same name already exists");
      }
    }

    // Change role is allowed only for txn xCluster configs.
    if (!xClusterConfig.getType().equals(ConfigType.Txn)
        && (Objects.nonNull(editFormData.sourceRole) || Objects.nonNull(editFormData.targetRole))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Changing xCluster role can be applied only to transactional xCluster configs");
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
    XClusterConfigRestartFormData restartFormData =
        parseRestartFormData(customerUUID, xClusterConfig, request);
    verifyTaskAllowed(xClusterConfig, TaskType.RestartXClusterConfig);
    Universe sourceUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getValidUniverseOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

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

    Pair<List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>, Set<String>>
        requestedTableInfoList_sourceTableIdsWithNoTableOnTargetUniverse =
            XClusterConfigTaskBase.getRequestedTableInfoListAndVerify(
                this.ybService,
                tableIds,
                bootstrapParams,
                sourceUniverse,
                targetUniverse,
                xClusterConfig.getReplicationGroupName(),
                xClusterConfig.getType());
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        requestedTableInfoList_sourceTableIdsWithNoTableOnTargetUniverse.getFirst();
    Set<String> sourceTableIdsWithNoTableOnTargetUniverse =
        requestedTableInfoList_sourceTableIdsWithNoTableOnTargetUniverse.getSecond();

    if (restartFormData.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    }

    // Submit task to edit xCluster config.
    XClusterConfigTaskParams params =
        new XClusterConfigTaskParams(
            xClusterConfig,
            bootstrapParams,
            requestedTableInfoList,
            mainTableIndexTablesMap,
            sourceTableIdsWithNoTableOnTargetUniverse,
            isForceDelete);
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

  /**
   * API that deletes an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "deleteXClusterConfig",
      value = "Delete xcluster config",
      response = YBPTask.class)
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

    Universe sourceUniverse = null;
    Universe targetUniverse = null;
    if (xClusterConfig.getSourceUniverseUUID() != null) {
      sourceUniverse =
          Universe.getValidUniverseOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    }
    if (xClusterConfig.getTargetUniverseUUID() != null) {
      targetUniverse =
          Universe.getValidUniverseOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);
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
      value = "Sync xcluster config",
      response = YBPTask.class)
  public Result sync(UUID customerUUID, UUID targetUniverseUUID, Http.Request request) {
    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigTaskParams params;
    Universe targetUniverse;
    if (targetUniverseUUID != null) {
      log.info("Received sync XClusterConfig request for universe({})", targetUniverseUUID);
      targetUniverse = Universe.getValidUniverseOrBadRequest(targetUniverseUUID, customer);
      params = new XClusterConfigTaskParams(targetUniverseUUID);
    } else {
      JsonNode requestBody = request.body().asJson();
      XClusterConfigSyncFormData formData =
          formFactory.getFormDataOrBadRequest(requestBody, XClusterConfigSyncFormData.class);
      log.info(
          "Received sync XClusterConfig request for universe({}) replicationGroupName({})",
          formData.targetUniverseUUID,
          formData.replicationGroupName);
      targetUniverse = Universe.getValidUniverseOrBadRequest(formData.targetUniverseUUID, customer);
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
  public Result needBootstrapTable(
      UUID customerUuid, UUID sourceUniverseUuid, String configTypeString, Http.Request request) {
    log.info(
        "Received needBootstrapTable request for sourceUniverseUuid={}, configTypeString={}",
        sourceUniverseUuid,
        configTypeString);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUuid);
    XClusterConfigNeedBootstrapFormData needBootstrapFormData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), XClusterConfigNeedBootstrapFormData.class);
    Universe.getValidUniverseOrBadRequest(sourceUniverseUuid, customer);
    needBootstrapFormData.tables =
        XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(needBootstrapFormData.tables);

    try {
      Map<String, Boolean> isBootstrapRequiredMap;
      isBootstrapRequiredMap =
          this.xClusterUniverseService.isBootstrapRequired(
              needBootstrapFormData.tables, null /* xClusterConfig */, sourceUniverseUuid);
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
        XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(needBootstrapFormData.tables);

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
      UUID customerUUID, XClusterConfig xClusterConfig, Http.Request request) {
    log.debug("Request body to restart an xCluster config is {}", request.body().asJson());
    XClusterConfigRestartFormData formData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), XClusterConfigRestartFormData.class);

    formData.tables = XClusterConfigTaskBase.convertTableUuidStringsToTableIdSet(formData.tables);

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

      if (xClusterConfig.getStatus() == XClusterConfig.XClusterConfigStatusType.Failed
          && formData.tables.size() < xClusterConfig.getTableIdsExcludeIndexTables().size()) {
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
              xClusterConfig.getStatus(),
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
          Set<String> tablesInReplication = config.getTableIds();
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
