package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getRequestedTableInfoList;

import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.XClusterScheduler;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.XClusterCreatePrecheck;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.XClusterUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.common.dr.DrConfigHelper.DrConfigTaskResult;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigEditForm;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigGetResp;
import com.yugabyte.yw.forms.DrConfigReplaceReplicaForm;
import com.yugabyte.yw.forms.DrConfigRestartForm;
import com.yugabyte.yw.forms.DrConfigSafetimeResp;
import com.yugabyte.yw.forms.DrConfigSafetimeResp.NamespaceSafetime;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigSetTablesForm;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigSyncFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Audit.ActionType;
import com.yugabyte.yw.models.Audit.TargetType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.XClusterTableConfig.Status;
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
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetUniverseReplicationInfoResponse;
import org.yb.client.GetXClusterOutboundReplicationGroupInfoResponse;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.*;
import org.yb.master.MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.NamespaceSafeTimePB;
import play.libs.Json;
import play.mvc.Http.Request;
import play.mvc.Result;

@Api(
    value = "Disaster Recovery",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class DrConfigController extends AuthenticatedController {

  private final Commissioner commissioner;
  private final MetricQueryHelper metricQueryHelper;
  private final DrConfigHelper drConfigHelper;
  private final YBClientService ybService;
  private final RuntimeConfGetter confGetter;
  private final XClusterUniverseService xClusterUniverseService;
  private final AutoFlagUtil autoFlagUtil;
  private final XClusterScheduler xClusterScheduler;
  private final UniverseTableHandler tableHandler;
  private final SoftwareUpgradeHelper softwareUpgradeHelper;
  private final XClusterCreatePrecheck xClusterCreatePrecheck;

  @Inject
  public DrConfigController(
      Commissioner commissioner,
      MetricQueryHelper metricQueryHelper,
      DrConfigHelper drConfigHelper,
      YBClientService ybService,
      RuntimeConfGetter confGetter,
      XClusterUniverseService xClusterUniverseService,
      AutoFlagUtil autoFlagUtil,
      XClusterScheduler xClusterScheduler,
      UniverseTableHandler tableHandler,
      SoftwareUpgradeHelper softwareUpgradeHelper,
      XClusterCreatePrecheck xClusterCreatePrecheck) {
    this.commissioner = commissioner;
    this.metricQueryHelper = metricQueryHelper;
    this.drConfigHelper = drConfigHelper;
    this.ybService = ybService;
    this.confGetter = confGetter;
    this.xClusterUniverseService = xClusterUniverseService;
    this.autoFlagUtil = autoFlagUtil;
    this.xClusterScheduler = xClusterScheduler;
    this.tableHandler = tableHandler;
    this.softwareUpgradeHelper = softwareUpgradeHelper;
    this.xClusterCreatePrecheck = xClusterCreatePrecheck;
  }

  /**
   * API that creates a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the task uuid that is creating the dr config
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "createDrConfig",
      value = "Create disaster recovery config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "disaster_recovery_create_form_data",
          value = "Disaster Recovery Create Form Data",
          dataType = "com.yugabyte.yw.forms.DrConfigCreateForm",
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
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result create(UUID customerUUID, Request request) {
    log.info("Received create drConfig request");

    // Parse and validate request.
    DrConfigCreateForm createForm = parseCreateForm(customerUUID, request);
    DrConfigTaskResult result = drConfigHelper.createDrConfigTask(customerUUID, createForm);
    UUID taskUUID = result.taskUuid();
    UUID drConfigUUID = result.drConfigUuid();
    String message = result.message();
    if (message != null) {
      return YBPSuccess.withMessage(message);
    }

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUUID.toString(),
            ActionType.Create,
            Json.toJson(createForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUUID).asResult();
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "editDrConfig",
      value = "Edit disaster recovery config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "disaster_recovery_edit_form_data",
          value = "Disaster Recovery Edit Form Data",
          dataType = "com.yugabyte.yw.forms.DrConfigEditForm",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result edit(UUID customerUUID, UUID drConfigUuid, Request request) {
    log.info("Received edit drConfig request");

    DrConfigEditForm editForm = parseEditForm(request);
    UUID taskUUID = drConfigHelper.editDrConfigTask(customerUUID, drConfigUuid, editForm);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUuid.toString(),
            ActionType.Edit,
            Json.toJson(editForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUuid).asResult();
  }

  /**
   * API that adds/removes tables to a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the dr config uuid
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "setTablesDrConfig",
      value = "Set tables in disaster recovery config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "disaster_recovery_set_tables_form_data",
          value = "Disaster Recovery Set Tables Form Data",
          dataType = "com.yugabyte.yw.forms.DrConfigSetTablesForm",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result setTables(UUID customerUUID, UUID drConfigUuid, Request request) {
    log.info("Received set tables drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    drConfigHelper.verifyTaskAllowed(drConfig, TaskType.SetTablesDrConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    DrConfigSetTablesForm setTablesForm = parseSetTablesForm(customerUUID, request);
    if (setTablesForm.bootstrapParams == null) {
      Set<String> tableIdsToAdd =
          XClusterConfigTaskBase.getTableIdsDiff(xClusterConfig.getTableIds(), setTablesForm.tables)
              .getFirst();
      if (!tableIdsToAdd.isEmpty()) {
        setTablesForm.bootstrapParams = drConfig.getBootstrapBackupParams();
      }
    }
    if (xClusterConfig.getType() == ConfigType.Db) {
      throw new PlatformServiceException(
          BAD_REQUEST, "This operation is not supported for db-scoped xCluster configs.");
    }
    XClusterConfigController.verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }

    BootstrapParams bootstrapParams =
        drConfigHelper.getBootstrapParamsFromRestartBootstrapParams(
            setTablesForm.bootstrapParams, setTablesForm.tables);
    XClusterConfigTaskParams taskParams =
        XClusterConfigController.getSetTablesTaskParams(
            ybService,
            xClusterConfig,
            sourceUniverse,
            targetUniverse,
            setTablesForm.tables,
            bootstrapParams,
            setTablesForm.autoIncludeIndexTables,
            false /* dryRun */,
            softwareUpgradeHelper);

    UUID taskUUID = commissioner.submit(TaskType.SetTablesDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Edit,
        drConfig.getName());
    log.info("Submitted set tables DrConfig({}), task {}", drConfig.getUuid(), taskUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfig.getUuid().toString(),
            ActionType.Edit,
            Json.toJson(setTablesForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfig.getUuid()).asResult();
  }

  /**
   * API that restart the xCluster config in a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the dr config uuid that is restarting the config
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "restartDrConfig",
      value = "Restart disaster recovery config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "disaster_recovery_restart_form_data",
          value = "Disaster Recovery Restart Form Data",
          dataType = "com.yugabyte.yw.forms.DrConfigRestartForm",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result restart(
      UUID customerUUID, UUID drConfigUuid, boolean isForceDelete, Request request) {
    log.info("Received restart drConfig request");

    // Todo: restart does not trigger bootstrapping. It does not remove extra xCluster configs.

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    drConfigHelper.verifyTaskAllowed(drConfig, TaskType.RestartDrConfig);

    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    DrConfigRestartForm restartForm = parseRestartForm(customerUUID, request);
    if (restartForm.bootstrapParams == null) {
      restartForm.bootstrapParams = drConfig.getBootstrapBackupParams();
    }
    XClusterConfigController.verifyTaskAllowed(xClusterConfig, TaskType.RestartXClusterConfig);
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }

    log.info("DR state is {}", drConfig.getState());

    XClusterConfigTaskParams taskParams;
    if (xClusterConfig.getType() != ConfigType.Db) {
      List<TableInfo> sourceTableInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);

      // Todo: Always add non existing tables to the xCluster config on restart.
      // Empty `dbs` field indicates a request to restart the entire config.
      // This is consistent with the restart xCluster config behaviour.
      Set<String> tableIds =
          CollectionUtils.isEmpty(restartForm.dbs)
              ? xClusterConfig.getTableIds()
              : XClusterConfigTaskBase.getTableIds(
                  getRequestedTableInfoList(restartForm.dbs, sourceTableInfoList));

      taskParams =
          XClusterConfigController.getRestartTaskParams(
              ybService,
              xClusterConfig,
              sourceUniverse,
              targetUniverse,
              tableIds,
              restartForm.bootstrapParams,
              false /* dryRun */,
              isForceDelete,
              drConfig.isHalted() /*isForceBootstrap*/,
              softwareUpgradeHelper);
    } else {
      taskParams =
          XClusterConfigController.getDbScopedRestartTaskParams(
              xClusterConfig,
              sourceUniverse,
              targetUniverse,
              restartForm.dbs,
              restartForm.bootstrapParams,
              drConfig.isHalted() /*isForceBootstrap*/,
              softwareUpgradeHelper);
    }

    UUID taskUUID = commissioner.submit(TaskType.RestartDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Restart,
        drConfig.getName());
    log.info("Submitted restart DrConfig({}), task {}", drConfig.getUuid(), taskUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfig.getUuid().toString(),
            ActionType.Restart,
            Json.toJson(restartForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfig.getUuid()).asResult();
  }

  /**
   * API that replace the replica universe a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the task uuid that is editing the dr config
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "replaceReplicaDrConfig",
      value = "Replace Replica in a disaster recovery config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "disaster_recovery_replace_replica_form_data",
          value = "Disaster Recovery Replace Replica Form Data",
          dataType = "com.yugabyte.yw.forms.DrConfigReplaceReplicaForm",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result replaceReplica(UUID customerUUID, UUID drConfigUuid, Request request) {
    log.info("Received replaceReplica drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    drConfigHelper.verifyTaskAllowed(drConfig, TaskType.EditDrConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    XClusterUtil.ensureUpgradeIsComplete(sourceUniverse, targetUniverse);

    DrConfigReplaceReplicaForm replaceReplicaForm =
        parseReplaceReplicaForm(customerUUID, sourceUniverse, targetUniverse, request);
    if (replaceReplicaForm.bootstrapParams == null) {
      replaceReplicaForm.bootstrapParams = drConfig.getBootstrapBackupParams();
    }
    Universe newTargetUniverse =
        Universe.getOrBadRequest(replaceReplicaForm.drReplicaUniverseUuid, customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkPromotedAutoFlagsEquality(sourceUniverse, newTargetUniverse);
    }

    DrConfigTaskParams taskParams;
    // Create xCluster config object.
    XClusterConfig newTargetXClusterConfig =
        drConfig.addXClusterConfig(
            sourceUniverse.getUniverseUUID(),
            newTargetUniverse.getUniverseUUID(),
            xClusterConfig.getType(),
            xClusterConfig.isAutomaticDdlMode());

    try {
      if (xClusterConfig.getType() != ConfigType.Db) {
        Set<String> tableIds = xClusterConfig.getTableIds();

        // Add index tables.
        Map<String, List<String>> mainTableIndexTablesMap =
            XClusterConfigTaskBase.getMainTableIndexTablesMap(
                this.ybService, sourceUniverse, tableIds);
        Set<String> indexTableIdSet =
            mainTableIndexTablesMap.values().stream()
                .flatMap(List::stream)
                .collect(Collectors.toSet());
        tableIds.addAll(indexTableIdSet);

        log.debug("tableIds are {}", tableIds);

        List<TableInfo> sourceTableInfoList =
            XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
        List<TableInfo> requestedTableInfoList =
            XClusterConfigTaskBase.filterTableInfoListByTableIds(sourceTableInfoList, tableIds);

        List<TableInfo> newTargetTableInfoList =
            XClusterConfigTaskBase.getTableInfoList(ybService, newTargetUniverse);
        Map<String, String> sourceTableIdNewTargetTableIdMap =
            XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
                requestedTableInfoList, newTargetTableInfoList);

        XClusterConfigTaskBase.verifyTablesNotInReplication(
            ybService,
            tableIds,
            xClusterConfig.getTableType(),
            ConfigType.Txn,
            sourceUniverse.getUniverseUUID(),
            sourceTableInfoList,
            newTargetUniverse.getUniverseUUID(),
            newTargetTableInfoList,
            true /* skipTxnReplicationCheck */);
        XClusterConfigController.certsForCdcDirGFlagCheck(sourceUniverse, newTargetUniverse);

        BootstrapParams bootstrapParams =
            drConfigHelper.getBootstrapParamsFromRestartBootstrapParams(
                replaceReplicaForm.bootstrapParams, tableIds);
        XClusterConfigController.xClusterBootstrappingPreChecks(
            requestedTableInfoList,
            sourceTableInfoList,
            newTargetUniverse,
            sourceUniverse,
            sourceTableIdNewTargetTableIdMap,
            ybService,
            bootstrapParams,
            null /* currentReplicationGroupName */);

        newTargetXClusterConfig.updateTables(tableIds, tableIds /* tableIdsNeedBootstrap */);
        newTargetXClusterConfig.updateIndexTablesFromMainTableIndexTablesMap(
            mainTableIndexTablesMap);
        taskParams =
            new DrConfigTaskParams(
                drConfig,
                xClusterConfig,
                newTargetXClusterConfig,
                bootstrapParams,
                requestedTableInfoList,
                mainTableIndexTablesMap,
                sourceTableIdNewTargetTableIdMap);
      } else {
        newTargetXClusterConfig.updateNamespaces(xClusterConfig.getDbIds());
        taskParams =
            new DrConfigTaskParams(
                drConfig,
                xClusterConfig,
                newTargetXClusterConfig,
                newTargetXClusterConfig.getDbIds(),
                Collections.emptyMap());
      }

      // Todo: add a dryRun option here.

      newTargetXClusterConfig.setSecondary(true);
      newTargetXClusterConfig.update();
    } catch (Exception e) {
      newTargetXClusterConfig.delete();
      throw e;
    }

    // Submit task to set up xCluster config.
    UUID taskUUID = commissioner.submit(TaskType.EditDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Edit,
        drConfig.getName());
    log.info("Submitted replaceReplica DrConfig({}), task {}", drConfig.getUuid(), taskUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfig.getUuid().toString(),
            ActionType.Edit,
            Json.toJson(replaceReplicaForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfig.getUuid()).asResult();
  }

  /**
   * API that runs switchover on a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the task uuid that is running on the dr config
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "switchoverDrConfig",
      value = "Switchover a disaster recovery config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "disaster_recovery_switchover_form_data",
          value = "Disaster Recovery Switchover Form Data",
          dataType = "com.yugabyte.yw.forms.DrConfigSwitchoverForm",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result switchover(UUID customerUUID, UUID drConfigUuid, Request request) {
    log.info("Received switchover drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfigSwitchoverForm switchoverForm = parseSwitchoverForm(request);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    drConfigHelper.verifyTaskAllowed(drConfig, TaskType.SwitchoverDrConfig);
    Optional<XClusterConfig> xClusterConfigOptional =
        drConfig.getActiveXClusterConfig(
            switchoverForm.drReplicaUniverseUuid, switchoverForm.primaryUniverseUuid);
    if (xClusterConfigOptional.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "The underlying xCluster config with source universe %s and target universe %s does"
                  + " not exist; possibly due to a previous switchover operation that has failed;"
                  + " you may retry that failed operation, or roll back.",
              switchoverForm.drReplicaUniverseUuid, switchoverForm.primaryUniverseUuid));
    }
    XClusterConfig xClusterConfig = xClusterConfigOptional.get();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    XClusterUtil.ensureUpgradeIsComplete(sourceUniverse, targetUniverse);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(targetUniverse, sourceUniverse);
    }

    // All the tables in DBs in replication on the source universe must be in the xCluster config.
    List<TableInfo> sourceTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);

    if (xClusterConfig.getType() != ConfigType.Db) {
      XClusterConfigTaskBase.validateSourceTablesInReplication(
          sourceTableInfoList, xClusterConfig.getTableIds());
    }

    XClusterConfig xClusterConfigTemp = XClusterConfig.getOrBadRequest(xClusterConfig.getUuid());
    xClusterScheduler.syncXClusterConfig(xClusterConfigTemp);
    xClusterConfigTemp.refresh();
    XClusterConfigTaskBase.updateReplicationDetailsFromDB(
        xClusterUniverseService,
        ybService,
        tableHandler,
        xClusterConfigTemp,
        confGetter.getGlobalConf(GlobalConfKeys.xclusterGetApiTimeoutMs),
        this.confGetter);
    // To do switchover, the xCluster config and all the tables in that config must be in
    // the green status because we are going to drop that config and the information for bad
    // replication streams will be lost.
    if (xClusterConfigTemp.getStatus() != XClusterConfigStatusType.Running
        || !xClusterConfigTemp.getTableDetails().stream()
            .map(XClusterTableConfig::getStatus)
            .allMatch(tableConfigStatus -> tableConfigStatus == Status.Running)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "In order to do switchover, the underlying xCluster config and all of its "
              + "replication streams must be in a running status. Go to the tables tab to see the "
              + "tables not in Running status.");
    }

    XClusterConfig switchoverXClusterConfig =
        drConfig.addXClusterConfig(
            xClusterConfig.getTargetUniverseUUID(),
            xClusterConfig.getSourceUniverseUUID(),
            xClusterConfig.getType(),
            xClusterConfig.isAutomaticDdlMode());
    switchoverXClusterConfig.setSecondary(true);
    switchoverXClusterConfig.update();

    // Todo: PLAT-10130, handle cases where the planned failover task fails.
    DrConfigTaskParams taskParams;
    List<TableInfo> targetTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);

    if (xClusterConfig.getType() != ConfigType.Db) {
      // Use table IDs on the target universe for failover xCluster.
      Map<String, String> sourceTableIdTargetTableIdMap =
          xClusterUniverseService.getSourceTableIdTargetTableIdMap(
              targetUniverse, xClusterConfig.getReplicationGroupName());
      Set<String> targetTableIds = new HashSet<>(sourceTableIdTargetTableIdMap.values());

      List<TableInfo> requestedTableInfoList =
          XClusterConfigTaskBase.filterTableInfoListByTableIds(targetTableInfoList, targetTableIds);

      // All tables must have corresponding tables on the target universe.
      Set<String> sourceTableIdsWithNoTableOnTargetUniverse =
          sourceTableIdTargetTableIdMap.entrySet().stream()
              .filter(entry -> Objects.isNull(entry.getValue()))
              .map(Entry::getKey)
              .collect(Collectors.toSet());
      if (!sourceTableIdsWithNoTableOnTargetUniverse.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "The following tables are in replication with no corresponding table on the target"
                    + " universe: %s. This can happen if the table is dropped without being removed"
                    + " from replication first. You may fix this issue by running `Reconcile config"
                    + " with DB` from UI",
                sourceTableIdsWithNoTableOnTargetUniverse));
      }

      drSwitchoverFailoverPreChecks(
          ybService,
          CustomerTask.TaskType.Switchover,
          requestedTableInfoList,
          targetTableInfoList,
          targetUniverse,
          sourceTableInfoList,
          sourceUniverse);

      Map<String, List<String>> mainTableIndexTablesMap =
          XClusterConfigTaskBase.getMainTableIndexTablesMap(
              ybService, targetUniverse, targetTableIds);

      switchoverXClusterConfig.updateTables(targetTableIds, null /* tableIdsNeedBootstrap */);
      switchoverXClusterConfig.updateIndexTablesFromMainTableIndexTablesMap(
          mainTableIndexTablesMap);
      taskParams =
          new DrConfigTaskParams(
              drConfig,
              xClusterConfig,
              switchoverXClusterConfig,
              null /* namespaceIdSafetimeEpochUsMap */,
              requestedTableInfoList,
              mainTableIndexTablesMap);
    } else {
      GetUniverseReplicationInfoResponse inboundReplicationResp;
      GetXClusterOutboundReplicationGroupInfoResponse outboundReplicationResp;

      if (xClusterConfig.isAutomaticDdlMode()) {
        // Hide the `replicated_ddls` table from the xCluster config. This table is metadata and
        // the user does not need to see it.
        sourceTableInfoList =
            sourceTableInfoList.stream()
                .filter(tableInfo -> !TableInfoUtil.isReplicatedDdlsTable(tableInfo))
                .collect(Collectors.toList());
        targetTableInfoList =
            targetTableInfoList.stream()
                .filter(tableInfo -> !TableInfoUtil.isReplicatedDdlsTable(tableInfo))
                .collect(Collectors.toList());
      }

      try {
        inboundReplicationResp =
            XClusterConfigTaskBase.getUniverseReplicationInfo(
                ybService, targetUniverse, xClusterConfig.getReplicationGroupName());
      } catch (Exception e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format(
                "Failed to get inbound replication group %s",
                xClusterConfig.getReplicationGroupName()));
      }

      try {
        outboundReplicationResp =
            XClusterConfigTaskBase.getXClusterOutboundReplicationGroupInfo(
                ybService, sourceUniverse, xClusterConfig.getReplicationGroupName());
      } catch (Exception e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format(
                "Failed to get outbound replication group %s",
                xClusterConfig.getReplicationGroupName()));
      }

      drDBScopedSwitchoverPreChecks(
          outboundReplicationResp,
          inboundReplicationResp,
          sourceTableInfoList,
          targetTableInfoList);

      switchoverXClusterConfig.updateNamespaces(
          inboundReplicationResp.getDbScopedInfos().stream()
              .map(DbScopedInfoPB::getTargetNamespaceId)
              .collect(Collectors.toSet()));

      taskParams =
          new DrConfigTaskParams(
              drConfig,
              xClusterConfig,
              switchoverXClusterConfig,
              switchoverXClusterConfig.getDbIds(),
              Collections.emptyMap());
    }

    switchoverXClusterConfig.update();

    // Submit task to set up xCluster config.
    UUID taskUUID = commissioner.submit(TaskType.SwitchoverDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Switchover,
        drConfig.getName());

    log.info("Submitted switchover DrConfig({}), task {}", drConfig.getUuid(), taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfig.getUuid().toString(),
            ActionType.Switchover,
            Json.toJson(switchoverForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfig.getUuid()).asResult();
  }

  /**
   * API that runs failover on a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the task uuid that is running on the dr config
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "failoverDrConfig",
      value = "Failover a disaster recovery config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "disaster_recovery_failover_form_data",
          value = "Disaster Recovery Failover Form Data",
          dataType = "com.yugabyte.yw.forms.DrConfigFailoverForm",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result failover(UUID customerUUID, UUID drConfigUuid, Request request) {
    log.info("Received failover drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfigFailoverForm failoverForm = parseFailoverForm(request);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    drConfigHelper.verifyTaskAllowed(drConfig, TaskType.FailoverDrConfig);
    Optional<XClusterConfig> xClusterConfigOptional =
        drConfig.getActiveXClusterConfig(
            failoverForm.drReplicaUniverseUuid, failoverForm.primaryUniverseUuid);
    if (xClusterConfigOptional.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "The underlying xCluster config with source universe %s and target universe %s does"
                  + " not exist; possibly due to a previous failover operation that has failed; you"
                  + " may retry that failed operation.",
              failoverForm.drReplicaUniverseUuid, failoverForm.primaryUniverseUuid));
    }
    XClusterConfig xClusterConfig = xClusterConfigOptional.get();
    // The following will be the new dr universe.
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    // The following will be the new primary universe.
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    if (!targetUniverse
        .getUniverseDetails()
        .softwareUpgradeState
        .equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot configure XCluster/DR config because target universe is not in ready state");
    }

    DrConfigTaskParams taskParams;
    Set<String> namespaceIdsWithSafetime =
        MapUtils.isEmpty(failoverForm.namespaceIdSafetimeEpochUsMap)
            ? null
            : failoverForm.namespaceIdSafetimeEpochUsMap.keySet();
    Set<String> namespaceIdsWithoutSafetime = null;
    XClusterConfig failoverXClusterConfig =
        drConfig.addXClusterConfig(
            xClusterConfig.getTargetUniverseUUID(),
            xClusterConfig.getSourceUniverseUUID(),
            xClusterConfig.getType(),
            xClusterConfig.isAutomaticDdlMode());

    try {
      if (xClusterConfig.getType() != ConfigType.Db) {
        List<TableInfo> targetTableInfoList =
            XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);

        // Because during failover, the source universe could be down, we should rely on the target
        // universe to get the table map between source to target.
        Map<String, String> sourceTableIdTargetTableIdMap =
            xClusterUniverseService.getSourceTableIdTargetTableIdMap(
                targetUniverse, xClusterConfig.getReplicationGroupName());

        // Use table IDs on the target universe for failover xCluster.
        Set<String> tableIds = new HashSet<>(sourceTableIdTargetTableIdMap.values());
        List<TableInfo> requestedTableInfoList =
            XClusterConfigTaskBase.filterTableInfoListByTableIds(targetTableInfoList, tableIds);

        // Todo: Add the following prechecks:
        //  1. XCluster controller create and add table: if a table is part of a DR config, it
        //   cannot be part of an xCluster config.
        //  2. Run certsForCdcDirGFlagCheck when creating the DR config on both directions.
        Map<String, List<String>> mainTableIndexTablesMap =
            XClusterConfigTaskBase.getMainTableIndexTablesMap(ybService, targetUniverse, tableIds);

        // If namespaceIdSafetimeEpochUsMap is passed in , make sure the safetime for all the
        // namespaces is specified.
        if (Objects.nonNull(namespaceIdsWithSafetime)) {
          namespaceIdsWithoutSafetime =
              XClusterConfigTaskBase.getNamespaces(requestedTableInfoList).stream()
                  .map(namespace -> namespace.getId().toStringUtf8())
                  .filter(namespaceId -> !namespaceIdsWithSafetime.contains(namespaceId))
                  .collect(Collectors.toSet());
        }

        taskParams =
            new DrConfigTaskParams(
                drConfig,
                xClusterConfig,
                failoverXClusterConfig,
                failoverForm.namespaceIdSafetimeEpochUsMap,
                requestedTableInfoList,
                mainTableIndexTablesMap);
        failoverXClusterConfig.updateTables(tableIds, null /* tableIdsNeedBootstrap */);
        failoverXClusterConfig.updateIndexTablesFromMainTableIndexTablesMap(
            mainTableIndexTablesMap);
      } else {
        try {
          Set<String> namespacesInReplication =
              XClusterConfigTaskBase.getUniverseReplicationInfo(
                      ybService, targetUniverse, xClusterConfig.getReplicationGroupName())
                  .getDbScopedInfos()
                  .stream()
                  .map(i -> i.getTargetNamespaceId())
                  .collect(Collectors.toSet());

          // If namespaceIdSafetimeEpochUsMap is passed in , make sure the safetime for all the
          // namespaces is specified.
          if (Objects.nonNull(namespaceIdsWithSafetime)) {
            namespaceIdsWithoutSafetime =
                Sets.difference(namespacesInReplication, namespaceIdsWithSafetime);
          }

          failoverXClusterConfig.updateNamespaces(namespacesInReplication);
        } catch (Exception e) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              String.format(
                  "Failed to get target namespace IDs for group %s",
                  xClusterConfig.getReplicationGroupName()));
        }

        taskParams =
            new DrConfigTaskParams(
                drConfig,
                xClusterConfig,
                failoverXClusterConfig,
                failoverXClusterConfig.getDbIds(),
                failoverForm.namespaceIdSafetimeEpochUsMap);
      }

      if (!CollectionUtils.isEmpty(namespaceIdsWithoutSafetime)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Safetime must be specified for all the databases in the disaster recovery "
                    + "config; namespaces ids without safetimes: %s",
                namespaceIdsWithoutSafetime));
      }

      failoverXClusterConfig.setSecondary(true);
      failoverXClusterConfig.update();
    } catch (Exception e) {
      failoverXClusterConfig.delete();
      throw e;
    }

    // Submit task to set up xCluster config.
    UUID taskUUID = commissioner.submit(TaskType.FailoverDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Failover,
        drConfig.getName());

    log.info("Submitted failover DrConfig({}), task {}", drConfig.getUuid(), taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfig.getUuid().toString(),
            ActionType.Failover,
            Json.toJson(failoverForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfig.getUuid()).asResult();
  }

  /**
   * API that gets a disaster recovery configuration.
   *
   * @return A form representing the requested dr config
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "getDrConfig",
      value = "Get disaster recovery config",
      response = DrConfigGetResp.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(
                path = "sourceUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result get(UUID customerUUID, UUID drUUID, boolean syncWithDB) {
    log.info("Received get DrConfig({}) request", drUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drUUID);

    XClusterConfig activeXClusterConfig = drConfig.getActiveXClusterConfig();
    if (syncWithDB) {
      xClusterScheduler.syncXClusterConfig(activeXClusterConfig);
      activeXClusterConfig.refresh();

      for (XClusterConfig xClusterConfig : drConfig.getXClusterConfigs()) {
        XClusterConfigTaskBase.updateReplicationDetailsFromDB(
            xClusterUniverseService,
            ybService,
            tableHandler,
            xClusterConfig,
            confGetter.getGlobalConf(GlobalConfKeys.xclusterGetApiTimeoutMs),
            this.confGetter);
      }
    }

    DrConfigGetResp resp = new DrConfigGetResp(drConfig, activeXClusterConfig);
    return PlatformResults.withData(resp);
  }

  /**
   * API that syncs the underlying xCluster config with the replication group in the target universe
   * cluster config.
   *
   * @return Result
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "syncDrConfig",
      value = "Sync disaster recovery config",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result sync(UUID customerUUID, UUID drConfigUuid, Request request) {
    log.info("Received sync drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    drConfigHelper.verifyTaskAllowed(drConfig, TaskType.SyncDrConfig);
    // This api will not work for the importing dr config. The config must already exist
    // in the yba db and we can sync the fields of the config.
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();

    XClusterConfigSyncFormData formData = new XClusterConfigSyncFormData();
    formData.targetUniverseUUID = xClusterConfig.getTargetUniverseUUID();
    formData.replicationGroupName = xClusterConfig.getReplicationGroupName();
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig, formData);

    UUID taskUUID = commissioner.submit(TaskType.SyncDrConfig, params);
    CustomerTask.create(
        customer,
        xClusterConfig.getSourceUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Sync,
        drConfig.getName());
    log.info("Submitted sync DrConfig for DrConfig({}), task {}", drConfig.getUuid(), taskUUID);
    auditService()
        .createAuditEntry(
            request,
            TargetType.DrConfig,
            drConfig.getUuid().toString(),
            ActionType.SyncDrConfig,
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that deletes a disaster recovery configuration.
   *
   * @return An instance of {@link YBPTask} indicating whether the task was created successfully
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "deleteXClusterConfig",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result delete(
      UUID customerUUID, UUID drConfigUuid, boolean isForceDelete, Request request) {
    log.info(
        "Received delete drConfig({}) request with isForceDelete={}", drConfigUuid, isForceDelete);

    UUID taskUUID = drConfigHelper.deleteDrConfigTask(customerUUID, drConfigUuid, isForceDelete);
    auditService()
        .createAuditEntry(
            request, TargetType.DrConfig, drConfigUuid.toString(), ActionType.Delete, taskUUID);
    return new YBPTask(taskUUID, drConfigUuid).asResult();
  }

  private Result toggleDrState(
      UUID customerUUID, UUID drConfigUUID, Request request, CustomerTask.TaskType taskType) {
    String operation = taskType == CustomerTask.TaskType.Resume ? "resume" : "pause";
    log.info("Received {} DrConfig({}) request", operation, drConfigUUID);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUUID);
    drConfigHelper.verifyTaskAllowed(drConfig, TaskType.EditXClusterConfig);
    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = taskType == CustomerTask.TaskType.Resume ? "Running" : "Paused";
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    XClusterConfigController.verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);

    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }

    XClusterConfigTaskParams params =
        new XClusterConfigTaskParams(
            xClusterConfig,
            editFormData,
            null /* requestedTableInfoList */,
            null /* mainTableToAddIndexTablesMap */,
            null /* tableIdsToAdd */,
            Collections.emptyMap() /* sourceTableIdTargetTableIdMap */,
            null /* tableIdsToRemove */);

    // Submit task to edit xCluster config.
    UUID taskUUID = commissioner.submit(TaskType.EditXClusterConfig, params);
    CustomerTask.create(
        customer,
        xClusterConfig.getSourceUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        taskType,
        drConfig.getName());

    log.info("Submitted {} DrConfig({}), task {}", operation, drConfigUUID, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUUID.toString(),
            taskType == CustomerTask.TaskType.Resume ? ActionType.Resume : ActionType.Pause,
            Json.toJson(editFormData),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUUID).asResult();
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "pauseDrConfig",
      value = "Pause DR config",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result pause(UUID customerUUID, UUID drConfigUUID, Request request) {
    return toggleDrState(customerUUID, drConfigUUID, request, CustomerTask.TaskType.Pause);
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "resumeDrConfig",
      value = "Resume DR config",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result resume(UUID customerUUID, UUID drConfigUUID, Request request) {
    return toggleDrState(customerUUID, drConfigUUID, request, CustomerTask.TaskType.Resume);
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "pauseDrUniverses",
      value = "Pause DR config and universes associated with DR",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.2.0.0")
  public Result pauseUniverses(UUID customerUUID, UUID drConfigUUID, Request request) {
    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUUID);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    pauseUniversesPrechecks(xClusterConfig, sourceUniverse, targetUniverse);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    String operation = "pause";
    editFormData.status = "Paused";

    XClusterConfigTaskParams params =
        new XClusterConfigTaskParams(
            xClusterConfig,
            editFormData,
            null /* requestedTableInfoList */,
            null /* mainTableToAddIndexTablesMap */,
            null /* tableIdsToAdd */,
            Collections.emptyMap() /* sourceTableIdTargetTableIdMap */,
            null /* tableIdsToRemove */);

    // Submit task to edit xCluster config.
    UUID taskUUID = commissioner.submit(TaskType.PauseXClusterUniverses, params);
    CustomerTask.create(
        customer,
        xClusterConfig.getSourceUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Pause,
        drConfig.getName());

    log.info("Submitted {} DrConfig({}) and universes, task {}", operation, drConfigUUID, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUUID.toString(),
            ActionType.Pause,
            Json.toJson(editFormData),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUUID).asResult();
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "resumeDrUniverses",
      value = "Resume DR config and universes associated with DR",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.2.0.0")
  public Result resumeUniverses(UUID customerUUID, UUID drConfigUUID, Request request) {
    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUUID);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    resumeUniversesPrechecks(xClusterConfig, sourceUniverse, targetUniverse);

    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    String operation = "resume";
    editFormData.status = "Running";

    XClusterConfigTaskParams params =
        new XClusterConfigTaskParams(
            xClusterConfig,
            editFormData,
            null /* requestedTableInfoList */,
            null /* mainTableToAddIndexTablesMap */,
            null /* tableIdsToAdd */,
            Collections.emptyMap() /* sourceTableIdTargetTableIdMap */,
            null /* tableIdsToRemove */);

    // Submit task to edit xCluster config.
    UUID taskUUID = commissioner.submit(TaskType.ResumeXClusterUniverses, params);
    CustomerTask.create(
        customer,
        xClusterConfig.getSourceUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Resume,
        drConfig.getName());

    log.info("Submitted {} DrConfig({}) and universes, task {}", operation, drConfigUUID, taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUUID.toString(),
            ActionType.Resume,
            Json.toJson(editFormData),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUUID).asResult();
  }

  /**
   * API that gets the safetime for a disaster recovery configuration.
   *
   * @return A form representing the safetimes for each namespace in the disaster recovery
   *     configuration and the min of those.
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "getDrConfigSafetime",
      value = "Get disaster recovery config safetime",
      response = DrConfigSafetimeResp.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(
                path = "sourceUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.1.0")
  public Result getSafetime(UUID customerUUID, UUID drUUID) {
    log.info("Received getSafetime DrConfig({}) request", drUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drUUID);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);
    List<NamespaceSafeTimePB> namespaceSafeTimeList =
        xClusterUniverseService.getNamespaceSafeTimeList(targetUniverse);

    DrConfigSafetimeResp safetimeResp = new DrConfigSafetimeResp();
    namespaceSafeTimeList.forEach(
        namespaceSafeTimePB -> {
          double estimatedDataLossMs = getEstimatedDataLossMs(targetUniverse, namespaceSafeTimePB);
          safetimeResp.safetimes.add(
              new NamespaceSafetime(namespaceSafeTimePB, estimatedDataLossMs));
        });
    return PlatformResults.withData(safetimeResp);
  }

  /**
   * API that adds/removes databases to a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the dr config uuid
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      nickname = "setDatabasesDrConfig",
      value = "Set databases in disaster recovery config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "disaster_recovery_set_databases_form_data",
          value = "Disaster Recovery Set Databases Form Data",
          dataType = "com.yugabyte.yw.forms.DrConfigSetDatabasesForm",
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
                identifier = "dr_configs",
                columnName = "dr_config_uuid")),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.XCLUSTER),
        resourceLocation =
            @Resource(
                path = "targetUniverseUUID",
                sourceType = SourceType.DB,
                dbClass = XClusterConfig.class,
                identifier = "dr_configs",
                columnName = "dr_config_uuid"))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  public Result setDatabases(UUID customerUUID, UUID drConfigUuid, Request request) {
    log.info("Received set databases drConfig request");
    DrConfigSetDatabasesForm setDatabasesForm = parseSetDatabasesForm(request);
    UUID taskUUID = drConfigHelper.setDatabasesTask(customerUUID, drConfigUuid, setDatabasesForm);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUuid.toString(),
            ActionType.Edit,
            Json.toJson(setDatabasesForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUuid).asResult();
  }

  private DrConfigCreateForm parseCreateForm(UUID customerUUID, Request request) {
    log.debug("Request body to create an DR config is {}", request.body().asJson());
    DrConfigCreateForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigCreateForm.class);
    if (Objects.equals(formData.sourceUniverseUUID, formData.targetUniverseUUID)) {
      throw new IllegalArgumentException(
          String.format(
              "Source and target universe cannot be the same: both are %s",
              formData.sourceUniverseUUID));
    }
    formData.dbs = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.dbs);
    drConfigHelper.validateBackupRequestParamsForBootstrapping(
        formData.bootstrapParams.backupRequestParams, customerUUID);
    return formData;
  }

  private DrConfigEditForm parseEditForm(Request request) {
    log.debug("Request body to edit a DR config is {}", request.body().asJson());
    return formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigEditForm.class);
  }

  private DrConfigSetTablesForm parseSetTablesForm(UUID customerUUID, Request request) {
    log.debug("Request body to set table a DR config is {}", request.body().asJson());
    DrConfigSetTablesForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigSetTablesForm.class);
    formData.tables = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.tables);
    if (Objects.nonNull(formData.bootstrapParams)) {
      drConfigHelper.validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);
    }
    return formData;
  }

  private DrConfigSetDatabasesForm parseSetDatabasesForm(Request request) {
    log.debug("Request body to set databases of a DR config is {}", request.body().asJson());
    DrConfigSetDatabasesForm formData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), DrConfigSetDatabasesForm.class);
    formData.dbs = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.dbs);
    return formData;
  }

  private DrConfigRestartForm parseRestartForm(UUID customerUUID, Request request) {
    log.debug("Request body to restart a DR config is {}", request.body().asJson());
    DrConfigRestartForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigRestartForm.class);
    formData.dbs = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.dbs);
    if (Objects.nonNull(formData.bootstrapParams)) {
      drConfigHelper.validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);
    }
    return formData;
  }

  private DrConfigReplaceReplicaForm parseReplaceReplicaForm(
      UUID customerUUID, Universe sourceUniverse, Universe targetUniverse, Request request) {
    log.debug("Request body to replace replica a DR config is {}", request.body().asJson());
    DrConfigReplaceReplicaForm formData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), DrConfigReplaceReplicaForm.class);
    if (formData.primaryUniverseUuid.equals(formData.drReplicaUniverseUuid)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "primaryUniverseUuid cannot be the same as drReplicaUniverseUuid");
    }
    if (targetUniverse.getUniverseUUID().equals(formData.drReplicaUniverseUuid)) {
      throw new IllegalArgumentException(
          "No change to the dr config detected; drReplicaUniverseUuid is the same as the "
              + "current standby universe");
    }
    if (!Objects.equals(sourceUniverse.getUniverseUUID(), formData.primaryUniverseUuid)) {
      throw new IllegalArgumentException(
          "primaryUniverseUuid must be the same as the current primary universe");
    }
    if (Objects.nonNull(formData.bootstrapParams)) {
      drConfigHelper.validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);
    }
    return formData;
  }

  private DrConfigSwitchoverForm parseSwitchoverForm(Request request) {
    log.debug("Request body to switchover a DR config is {}", request.body().asJson());
    DrConfigSwitchoverForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigSwitchoverForm.class);

    if (formData.primaryUniverseUuid.equals(formData.drReplicaUniverseUuid)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "primaryUniverseUuid cannot be the same as drReplicaUniverseUuid");
    }

    return formData;
  }

  private DrConfigFailoverForm parseFailoverForm(Request request) {
    log.debug("Request body to failover a DR config is {}", request.body().asJson());
    DrConfigFailoverForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigFailoverForm.class);

    if (formData.primaryUniverseUuid.equals(formData.drReplicaUniverseUuid)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "primaryUniverseUuid cannot be the same as drReplicaUniverseUuid");
    }

    return formData;
  }

  /**
   * It runs some pre-checks to ensure that the reverse direction xCluster config can be set up. A
   * reverse direction xCluster config is almost the same as the main xCluster config but in the
   * reverse direction.
   *
   * @param ybClientService The YB client service to use for the pre-checks.
   * @param taskType This specifies the task that triggered the creation of a reverse direction
   *     xCluster config.
   * @param requestedTableInfoList The table info list on the target universe that will be part of
   *     the failover xCluster config
   * @param targetTableInfoList The table info list for all tables on the target universe
   * @param targetUniverse The target universe in the main xCluster config which is the same as the
   *     source universe in the reverse direction xCluster config
   * @param sourceTableInfoList The table info list for all tables on the source universe
   * @param sourceUniverse The source universe in the main xCluster config which is the same as the
   *     target universe in the reverse direction xCluster config
   */
  public static void drSwitchoverFailoverPreChecks(
      YBClientService ybClientService,
      CustomerTask.TaskType taskType,
      List<TableInfo> requestedTableInfoList,
      List<TableInfo> targetTableInfoList,
      Universe targetUniverse,
      List<TableInfo> sourceTableInfoList,
      Universe sourceUniverse) {
    Set<String> tableIds = XClusterConfigTaskBase.getTableIds(requestedTableInfoList);

    // General xCluster pre-checks.
    XClusterConfigTaskBase.verifyTablesNotInReplication(
        ybClientService,
        tableIds,
        TableInfoUtil.getXClusterConfigTableType(requestedTableInfoList),
        ConfigType.Txn,
        targetUniverse.getUniverseUUID(),
        targetTableInfoList,
        sourceUniverse.getUniverseUUID(),
        sourceTableInfoList,
        true /* skipTxnReplicationCheck */);
    XClusterConfigController.certsForCdcDirGFlagCheck(targetUniverse, sourceUniverse);

    // If table type is YSQL, all tables in that keyspace are selected.
    if (XClusterConfigTaskBase.getTableType(requestedTableInfoList) == TableType.PGSQL_TABLE_TYPE) {
      XClusterConfigTaskBase.validateTargetTablesInReplication(
          targetTableInfoList,
          XClusterConfigTaskBase.getTableIds(requestedTableInfoList),
          taskType);
    }
  }

  public static void drDBScopedSwitchoverPreChecks(
      GetXClusterOutboundReplicationGroupInfoResponse outboundReplicationResp,
      GetUniverseReplicationInfoResponse inboundReplicationResp,
      List<TableInfo> sourceTableInfoList,
      List<TableInfo> targetTableInfoList) {

    Map<String, String> inboundSourceToTargetTableId =
        inboundReplicationResp.getTableInfos().stream()
            .collect(
                Collectors.toMap(TableInfoPB::getSourceTableId, TableInfoPB::getTargetTableId));
    Set<String> inboundSourceTableIds = inboundSourceToTargetTableId.keySet();
    Set<String> outboundSourceTableIds =
        outboundReplicationResp.getNamespaceInfos().stream()
            .map(namespaceInfo -> namespaceInfo.getTableStreamsMap().keySet())
            .flatMap(Set::stream)
            .collect(Collectors.toSet());

    XClusterConfigTaskBase.validateOutInboundReplicationTables(
        outboundSourceTableIds, inboundSourceTableIds);

    XClusterConfigTaskBase.validateSourceTablesInReplication(
        sourceTableInfoList, outboundSourceTableIds);
    XClusterConfigTaskBase.validateTargetTablesInReplication(
        targetTableInfoList,
        new HashSet<>(inboundSourceToTargetTableId.values()),
        CustomerTask.TaskType.Switchover);
  }

  private double getEstimatedDataLossMs(
      Universe targetUniverse, NamespaceSafeTimePB namespaceSafeTimePB) {
    // -1 means could not find it from Prometheus.
    double estimatedDataLossMs = -1;
    try {
      long safetimeEpochSeconds =
          Duration.ofNanos(
                  NamespaceSafetime.computeSafetimeEpochUsFromSafeTimeHt(
                          namespaceSafeTimePB.getSafeTimeHt())
                      * 1000)
              .getSeconds();
      String promQuery =
          String.format(
              "%s{export_type=\"master_export\",universe_uuid=\"%s\","
                  + "node_address=\"%s\",namespace_id=\"%s\"}@%s",
              XClusterConfigTaskBase.TXN_XCLUSTER_SAFETIME_LAG_NAME,
              targetUniverse.getUniverseUUID().toString(),
              targetUniverse.getMasterLeaderHostText(),
              namespaceSafeTimePB.getNamespaceId(),
              safetimeEpochSeconds);
      ArrayList<MetricQueryResponse.Entry> queryResult =
          this.metricQueryHelper.queryDirect(promQuery);
      log.debug("Response to query {} is {}", promQuery, queryResult);
      if (queryResult.size() != 1) {
        log.error(
            "Could not get the estimatedDataLoss: Prometheus did not return only one entry:"
                + " {}",
            queryResult);
        return estimatedDataLossMs;
      }
      MetricQueryResponse.Entry metricEntry = queryResult.get(0);
      if (metricEntry.values.isEmpty()) {
        log.error(
            "Could not get the estimatedDataLoss: no value exists for the metric entry: {}",
            queryResult);
        return estimatedDataLossMs;
      }
      estimatedDataLossMs =
          metricEntry.values.stream()
              .min(Comparator.comparing(ImmutablePair::getLeft))
              .map(valueEntry -> valueEntry.getRight())
              .orElse(-1d);
      if (estimatedDataLossMs == -1) {
        log.error(
            "Could not get the estimatedDataLoss: could not identify the value with"
                + " minimum key: {}",
            queryResult);
      }
    } catch (Exception e) {
      log.error("Could not get the estimatedDataLoss: {}", e.getMessage());
    }
    return estimatedDataLossMs;
  }

  public static void verifyTaskAllowed(DrConfig drConfig, TaskType taskType) {
    if (!XClusterConfigTaskBase.isTaskAllowed(drConfig, taskType)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s task is not allowed; with state `%s`, the allowed tasks are %s",
              taskType, drConfig.getState(), XClusterConfigTaskBase.getAllowedTasks(drConfig)));
    }
  }

  public void pauseUniversesPrechecks(
      XClusterConfig xClusterConfig, Universe sourceUniverse, Universe targetUniverse) {
    if (xClusterConfig.getStatus() != XClusterConfigStatusType.Running) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "The DR Config status be in running state. Current status is: %s.",
              xClusterConfig.getStatus()));
    }

    if (xClusterConfig.isPaused()) {
      throw new PlatformServiceException(BAD_REQUEST, "DR Config is already paused");
    }

    if (sourceUniverse.getUniverseDetails().universePaused
        || targetUniverse.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Either universe %s or %s are already paused",
              sourceUniverse.getName(), targetUniverse.getName()));
    }
  }

  public void resumeUniversesPrechecks(
      XClusterConfig xClusterConfig, Universe sourceUniverse, Universe targetUniverse) {
    if (!xClusterConfig.isPaused()) {
      throw new PlatformServiceException(BAD_REQUEST, "DR Config is expected to be paused");
    }

    if (!(sourceUniverse.getUniverseDetails().universePaused
        && targetUniverse.getUniverseDetails().universePaused)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "One of universes %s or %s are not paused. Both universes are expected to be paused.",
              sourceUniverse.getName(), targetUniverse.getName()));
    }
  }
}
