package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getRequestedTableInfoList;
import static org.yb.master.MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.*;

import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.XClusterScheduler;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.XClusterUtil;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
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
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
import com.yugabyte.yw.forms.XClusterConfigSyncFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Audit;
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
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.NamespaceSafeTimePB;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Disaster Recovery",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class DrConfigController extends AuthenticatedController {

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
  public DrConfigController(
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
   * API that creates a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the task uuid that is creating the dr config
   */
  @ApiOperation(
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
  public Result create(UUID customerUUID, Http.Request request) {
    log.info("Received create drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfigCreateForm createForm = parseCreateForm(customerUUID, request);
    Universe sourceUniverse = Universe.getOrBadRequest(createForm.sourceUniverseUUID, customer);
    Universe targetUniverse = Universe.getOrBadRequest(createForm.targetUniverseUUID, customer);

    if (!confGetter.getGlobalConf(GlobalConfKeys.disasterRecoveryEnabled)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Support for disaster recovery configs is disabled in YBA. You may enable it "
              + "by setting yb.xcluster.dr.enabled to true in the application.conf");
    }
    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkPromotedAutoFlagsEquality(sourceUniverse, targetUniverse);
    }

    boolean isDbScoped =
        confGetter.getGlobalConf(GlobalConfKeys.dbScopedXClusterEnabled) || createForm.dbScoped;
    if (!confGetter.getGlobalConf(GlobalConfKeys.dbScopedXClusterEnabled) && createForm.dbScoped) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Support for db scoped disaster recovery configs is disabled in YBA. You may enable it "
              + "by setting yb.xcluster.db_scoped.enabled to true in the application.conf");
    }

    if (isDbScoped) {
      XClusterUtil.dbScopedXClusterPreChecks(sourceUniverse, targetUniverse);
    }

    DrConfig drConfig;
    DrConfigTaskParams taskParams;
    if (isDbScoped) {
      if (createForm.dryRun) {
        return YBPSuccess.withMessage("The pre-checks are successful");
      }

      drConfig =
          DrConfig.create(
              createForm.name,
              createForm.sourceUniverseUUID,
              createForm.targetUniverseUUID,
              createForm.bootstrapParams.backupRequestParams,
              createForm.dbs);

      taskParams =
          new DrConfigTaskParams(
              drConfig,
              getBootstrapParamsFromRestartBootstrapParams(
                  createForm.bootstrapParams, new HashSet<>()),
              createForm.dbs,
              createForm.pitrParams);
    } else {
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
          getRequestedTableInfoList(createForm.dbs, sourceTableInfoList);

      Set<String> tableIds = XClusterConfigTaskBase.getTableIds(requestedTableInfoList);
      Map<String, List<String>> mainTableIndexTablesMap =
          XClusterConfigTaskBase.getMainTableIndexTablesMap(
              this.ybService, sourceUniverse, tableIds);

      XClusterConfigController.xClusterCreatePreChecks(
          requestedTableInfoList, ConfigType.Txn, sourceUniverse, targetUniverse, confGetter);

      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);
      Map<String, String> sourceTableIdTargetTableIdMap =
          XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
              requestedTableInfoList, targetTableInfoList);

      BootstrapParams bootstrapParams =
          getBootstrapParamsFromRestartBootstrapParams(createForm.bootstrapParams, tableIds);
      XClusterConfigController.xClusterBootstrappingPreChecks(
          requestedTableInfoList,
          sourceTableInfoList,
          targetUniverse,
          sourceUniverse,
          sourceTableIdTargetTableIdMap,
          ybService,
          bootstrapParams,
          null /* currentReplicationGroupName */);

      if (createForm.dryRun) {
        return YBPSuccess.withMessage("The pre-checks are successful");
      }

      // Todo: Ensure the PITR parameters have the right RPOs.

      // Create xCluster config object.
      drConfig =
          DrConfig.create(
              createForm.name,
              createForm.sourceUniverseUUID,
              createForm.targetUniverseUUID,
              tableIds,
              createForm.bootstrapParams.backupRequestParams);
      drConfig
          .getActiveXClusterConfig()
          .updateIndexTablesFromMainTableIndexTablesMap(mainTableIndexTablesMap);

      // Submit task to set up xCluster config.
      taskParams =
          new DrConfigTaskParams(
              drConfig,
              bootstrapParams,
              requestedTableInfoList,
              mainTableIndexTablesMap,
              sourceTableIdTargetTableIdMap,
              createForm.pitrParams);
    }

    UUID taskUUID = commissioner.submit(TaskType.CreateDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Create,
        drConfig.getName());

    log.info("Submitted create DrConfig({}), task {}", drConfig.getUuid(), taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.Create,
            Json.toJson(createForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfig.getUuid()).asResult();
  }

  @ApiOperation(
      nickname = "editDrConfig",
      value = "Edit disaster recovery config",
      response = DrConfig.class)
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
  public Result edit(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received edit drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    disallowActionOnErrorState(drConfig);

    DrConfigEditForm editForm = parseEditForm(request);
    validateEditForm(editForm, customer.getUuid(), drConfig);

    drConfig.setStorageConfigUuid(editForm.bootstrapParams.backupRequestParams.storageConfigUUID);
    drConfig.setParallelism(editForm.bootstrapParams.backupRequestParams.parallelism);
    drConfig.update();

    DrConfigGetResp resp = new DrConfigGetResp(drConfig, drConfig.getActiveXClusterConfig());
    return PlatformResults.withData(resp);
  }

  /**
   * API that adds/removes tables to a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the dr config uuid
   */
  @ApiOperation(
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
  public Result setTables(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received set tables drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    disallowActionOnErrorState(drConfig);
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
    XClusterConfigController.verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }

    BootstrapParams bootstrapParams =
        getBootstrapParamsFromRestartBootstrapParams(
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
            false /* dryRun */);

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
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.Edit,
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
  public Result restart(
      UUID customerUUID, UUID drConfigUuid, boolean isForceDelete, Http.Request request) {
    log.info("Received restart drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    disallowActionOnErrorState(drConfig);

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
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
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
              drConfig.isHalted() /*isForceBootstrap*/);
    } else {
      taskParams =
          XClusterConfigController.getDbScopedRestartTaskParams(
              ybService,
              xClusterConfig,
              sourceUniverse,
              targetUniverse,
              restartForm.dbs,
              restartForm.bootstrapParams,
              false /* dryRun */,
              isForceDelete,
              drConfig.isHalted() /*isForceBootstrap*/);
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
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.Restart,
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
  public Result replaceReplica(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received replaceReplica drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    disallowActionOnErrorState(drConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

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
            xClusterConfig.getType());

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

        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
            XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
            XClusterConfigTaskBase.filterTableInfoListByTableIds(sourceTableInfoList, tableIds);

        XClusterConfigTaskBase.verifyTablesNotInReplication(
            tableIds, sourceUniverse.getUniverseUUID(), newTargetUniverse.getUniverseUUID());
        XClusterConfigController.certsForCdcDirGFlagCheck(sourceUniverse, newTargetUniverse);

        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> newTargetTableInfoList =
            XClusterConfigTaskBase.getTableInfoList(ybService, newTargetUniverse);
        Map<String, String> sourceTableIdNewTargetTableIdMap =
            XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
                requestedTableInfoList, newTargetTableInfoList);

        BootstrapParams bootstrapParams =
            getBootstrapParamsFromRestartBootstrapParams(
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
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.Edit,
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
  public Result switchover(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received switchover drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfigSwitchoverForm switchoverForm = parseSwitchoverForm(request);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    disallowActionOnErrorState(drConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(targetUniverse, sourceUniverse);
    }

    // All the tables in DBs in replication on the source universe must be in the xCluster config.
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);
    XClusterConfigTaskBase.groupByNamespaceId(
            XClusterConfigTaskBase.filterTableInfoListByTableIds(
                sourceTableInfoList, xClusterConfig.getTableIds()))
        .forEach(
            (namespaceId, tablesInfoList) -> {
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
                      .map(XClusterConfigTaskBase::getTableId)
                      .collect(Collectors.toSet());
              Set<String> tableIdsNotInReplication =
                  tableIdsInNamespace.stream()
                      .filter(
                          tableId ->
                              !XClusterConfigTaskBase.getTableIds(tablesInfoList).contains(tableId))
                      .collect(Collectors.toSet());
              if (!tableIdsNotInReplication.isEmpty()) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format(
                        "To do a switchover, all the tables in a keyspace that exist on the source"
                            + " universe and support xCluster replication must be in replication:"
                            + " missing table ids: %s in the keyspace: %s",
                        tableIdsNotInReplication, namespaceId));
              }
            });

    // To do switchover, the xCluster config and all the tables in that config must be in
    // the green status because we are going to drop that config and the information for bad
    // replication streams will be lost.
    if (xClusterConfig.getStatus() != XClusterConfigStatusType.Running
        || !xClusterConfig.getTableDetails().stream()
            .map(XClusterTableConfig::getStatus)
            .allMatch(tableConfigStatus -> tableConfigStatus == Status.Running)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "In order to do switchover, the underlying xCluster config and all of its "
              + "replication streams must be in a running status. Please either restart the config "
              + "to put everything in a working state, or if the xCluster config is in a running "
              + "status, you can remove the tables whose replication is broken to run switchover.");
    }

    XClusterConfig switchoverXClusterConfig =
        drConfig.addXClusterConfig(
            xClusterConfig.getTargetUniverseUUID(),
            xClusterConfig.getSourceUniverseUUID(),
            xClusterConfig.getType());

    // Todo: PLAT-10130, handle cases where the planned failover task fails.
    DrConfigTaskParams taskParams;

    if (xClusterConfig.getType() != ConfigType.Db) {
      // Use table IDs on the target universe for failover xCluster.
      Map<String, String> sourceTableIdTargetTableIdMap =
          xClusterUniverseService.getSourceTableIdTargetTableIdMap(
              targetUniverse, xClusterConfig.getReplicationGroupName());
      Set<String> targetTableIds = new HashSet<>(sourceTableIdTargetTableIdMap.values());
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList =
          XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);

      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
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
          CustomerTask.TaskType.Switchover,
          requestedTableInfoList,
          targetTableInfoList,
          targetUniverse,
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
      try {
        switchoverXClusterConfig.updateNamespaces(
            XClusterConfigTaskBase.getUniverseReplicationInfo(
                    ybService, targetUniverse, xClusterConfig.getReplicationGroupName())
                .getDbScopedInfos()
                .stream()
                .map(DbScopedInfoPB::getTargetNamespaceId)
                .collect(Collectors.toSet()));
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
              switchoverXClusterConfig,
              switchoverXClusterConfig.getDbIds(),
              Collections.emptyMap());
    }

    switchoverXClusterConfig.setSecondary(true);
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
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.Switchover,
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
  public Result failover(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received failover drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfigFailoverForm failoverForm = parseFailoverForm(request);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    disallowActionOnErrorState(drConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    DrConfigTaskParams taskParams;
    Set<String> namespaceIdsWithSafetime = failoverForm.namespaceIdSafetimeEpochUsMap.keySet();
    Set<String> namespaceIdsWithoutSafetime;
    // Todo: Add pre-checks for user's input safetime.
    XClusterConfig failoverXClusterConfig =
        drConfig.addXClusterConfig(
            xClusterConfig.getTargetUniverseUUID(),
            xClusterConfig.getSourceUniverseUUID(),
            xClusterConfig.getType());

    try {
      if (xClusterConfig.getType() != ConfigType.Db) {
        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList =
            XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);

        // Because during failover, the source universe could be down, we should rely on the target
        // universe to get the table map between source to target.
        Map<String, String> sourceTableIdTargetTableIdMap =
            xClusterUniverseService.getSourceTableIdTargetTableIdMap(
                targetUniverse, xClusterConfig.getReplicationGroupName());

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
                  "The following tables are in replication with no corresponding table on the"
                      + " target universe: %s. This can happen if the table is dropped without"
                      + " being removed from replication first. You may fix this issue by running"
                      + " `Reconcile config with DB` from UI",
                  sourceTableIdsWithNoTableOnTargetUniverse));
        }

        // Use table IDs on the target universe for failover xCluster.
        Set<String> tableIds = new HashSet<>(sourceTableIdTargetTableIdMap.values());
        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
            XClusterConfigTaskBase.filterTableInfoListByTableIds(targetTableInfoList, tableIds);

        drSwitchoverFailoverPreChecks(
            CustomerTask.TaskType.Failover,
            requestedTableInfoList,
            targetTableInfoList,
            targetUniverse,
            sourceUniverse);
        Map<String, List<String>> mainTableIndexTablesMap =
            XClusterConfigTaskBase.getMainTableIndexTablesMap(ybService, targetUniverse, tableIds);

        // Make sure the safetime for all the namespaces is specified.
        namespaceIdsWithoutSafetime =
            XClusterConfigTaskBase.getNamespaces(requestedTableInfoList).stream()
                .map(namespace -> namespace.getId().toStringUtf8())
                .filter(namespaceId -> !namespaceIdsWithSafetime.contains(namespaceId))
                .collect(Collectors.toSet());
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
          namespaceIdsWithoutSafetime =
              Sets.difference(namespacesInReplication, namespaceIdsWithSafetime);

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
    } catch (Exception e) {
      failoverXClusterConfig.delete();
      throw e;
    }

    if (!namespaceIdsWithoutSafetime.isEmpty()) {
      failoverXClusterConfig.delete();
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Safetime must be specified for all the databases in the disaster recovery "
                  + "config; namespaces ids without safetimes: %s",
              namespaceIdsWithoutSafetime));
    }

    failoverXClusterConfig.setSecondary(true);
    failoverXClusterConfig.update();

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
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.Failover,
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
  public Result get(UUID customerUUID, UUID drUUID) {
    log.info("Received get DrConfig({}) request", drUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drUUID);

    XClusterConfig activeXClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterScheduler.syncXClusterConfig(activeXClusterConfig);
    activeXClusterConfig.refresh();

    for (XClusterConfig xClusterConfig : drConfig.getXClusterConfigs()) {
      XClusterConfigTaskBase.updateReplicationDetailsFromDB(
          this.xClusterUniverseService, this.ybService, this.tableHandler, xClusterConfig);
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
  public Result sync(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received sync drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();

    XClusterConfigSyncFormData formData = new XClusterConfigSyncFormData();
    formData.targetUniverseUUID = xClusterConfig.getTargetUniverseUUID();
    formData.replicationGroupName = xClusterConfig.getReplicationGroupName();
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(formData);

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
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.SyncDrConfig,
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that deletes a disaster recovery configuration.
   *
   * @return An instance of {@link YBPTask} indicating whether the task was created successfully
   */
  @ApiOperation(
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
  public Result delete(
      UUID customerUUID, UUID drConfigUuid, boolean isForceDelete, Http.Request request) {
    log.info(
        "Received delete drConfig({}) request with isForceDelete={}", drConfigUuid, isForceDelete);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    XClusterConfigController.verifyTaskAllowed(xClusterConfig, TaskType.DeleteXClusterConfig);
    Universe sourceUniverse = null;
    Universe targetUniverse = null;
    if (xClusterConfig.getSourceUniverseUUID() != null) {
      sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    }
    if (xClusterConfig.getTargetUniverseUUID() != null) {
      targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);
    }

    // Submit task to delete DR config.
    DrConfigTaskParams params = new DrConfigTaskParams(drConfig, isForceDelete);
    UUID taskUUID = commissioner.submit(TaskType.DeleteDrConfig, params);
    if (sourceUniverse != null) {
      CustomerTask.create(
          customer,
          sourceUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.DrConfig,
          CustomerTask.TaskType.Delete,
          drConfig.getName());
    } else if (targetUniverse != null) {
      CustomerTask.create(
          customer,
          targetUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.DrConfig,
          CustomerTask.TaskType.Delete,
          drConfig.getName());
    }
    log.info("Submitted delete drConfig({}), task {}", drConfigUuid, taskUUID);

    auditService()
        .createAuditEntry(
            request,
            TargetType.DrConfig,
            drConfigUuid.toString(),
            Audit.ActionType.Delete,
            taskUUID);
    return new YBPTask(taskUUID, drConfigUuid).asResult();
  }

  private Result toggleDrState(
      UUID customerUUID, UUID drConfigUUID, Http.Request request, CustomerTask.TaskType taskType) {
    String operation = taskType == CustomerTask.TaskType.Resume ? "resume" : "pause";
    log.info("Received {} DrConfig({}) request", operation, drConfigUUID);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUUID);
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
            Audit.TargetType.DrConfig,
            drConfigUUID.toString(),
            taskType == CustomerTask.TaskType.Resume
                ? Audit.ActionType.Resume
                : Audit.ActionType.Pause,
            Json.toJson(editFormData),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUUID).asResult();
  }

  @ApiOperation(nickname = "pauseDrConfig", value = "Pause DR config", response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.PAUSE_RESUME),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result pause(UUID customerUUID, UUID drConfigUUID, Http.Request request) {
    return toggleDrState(customerUUID, drConfigUUID, request, CustomerTask.TaskType.Pause);
  }

  @ApiOperation(nickname = "resumeDrConfig", value = "Resume DR config", response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.PAUSE_RESUME),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result resume(UUID customerUUID, UUID drConfigUUID, Http.Request request) {
    return toggleDrState(customerUUID, drConfigUUID, request, CustomerTask.TaskType.Resume);
  }

  /**
   * API that gets the safetime for a disaster recovery configuration.
   *
   * @return A form representing the safetimes for each namespace in the disaster recovery
   *     configuration and the min of those.
   */
  @ApiOperation(
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
  public Result getSafetime(UUID customerUUID, UUID drUUID) {
    log.info("Received getSafetime DrConfig({}) request", drUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drUUID);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);
    List<NamespaceSafeTimePB> namespaceSafeTimeList =
        xClusterUniverseService.getNamespaceSafeTimeList(xClusterConfig);

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
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  public Result setDatabases(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received set databases drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    disallowActionOnErrorState(drConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);
    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }
    DrConfigSetDatabasesForm setDatabasesForm = parseSetDatabasesForm(customerUUID, request);
    Set<String> existingDatabaseIds = xClusterConfig.getDbIds();
    Set<String> newDatabaseIds = setDatabasesForm.databases;
    Set<String> databaseIdsToAdd = Sets.difference(newDatabaseIds, existingDatabaseIds);
    Set<String> databaseIdsToRemove = Sets.difference(existingDatabaseIds, newDatabaseIds);
    if (databaseIdsToAdd.isEmpty() && databaseIdsToRemove.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "The list of new databases to add/remove is empty.");
    }

    XClusterConfigController.verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);

    XClusterConfigRestartFormData.RestartBootstrapParams restartBootstrapParams =
        drConfig.getBootstrapBackupParams();
    BootstrapParams bootstrapParams =
        getBootstrapParamsFromRestartBootstrapParams(restartBootstrapParams, null);

    XClusterConfigTaskParams taskParams =
        XClusterConfigController.getSetDatabasesTaskParams(
            xClusterConfig, bootstrapParams, newDatabaseIds, databaseIdsToAdd, databaseIdsToRemove);

    UUID taskUUID = commissioner.submit(TaskType.SetDatabasesDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Edit,
        drConfig.getName());
    log.info("Submitted set databases DrConfig({}), task {}", drConfig.getUuid(), taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.Edit,
            Json.toJson(setDatabasesForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfig.getUuid()).asResult();
  }

  private DrConfigCreateForm parseCreateForm(UUID customerUUID, Http.Request request) {
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
    validateBackupRequestParamsForBootstrapping(
        formData.bootstrapParams.backupRequestParams, customerUUID);
    return formData;
  }

  private DrConfigEditForm parseEditForm(Http.Request request) {
    log.debug("Request body to edit a DR config is {}", request.body().asJson());
    return formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigEditForm.class);
  }

  private void validateEditForm(DrConfigEditForm formData, UUID customerUUID, DrConfig drConfig) {
    validateBackupRequestParamsForBootstrapping(
        formData.bootstrapParams.backupRequestParams, customerUUID);

    UUID newStorageConfigUUID = formData.bootstrapParams.backupRequestParams.storageConfigUUID;
    int newParallelism = formData.bootstrapParams.backupRequestParams.parallelism;
    if (drConfig.getStorageConfigUuid().equals(newStorageConfigUUID)
        && drConfig.getParallelism() == newParallelism) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "No changes were made to drConfig. Current Storage configuration with uuid: %s and"
                  + " parallelism: %d for drConfig: %s",
              drConfig.getStorageConfigUuid(), drConfig.getParallelism(), drConfig.getName()));
    }
  }

  private DrConfigSetTablesForm parseSetTablesForm(UUID customerUUID, Http.Request request) {
    log.debug("Request body to set table a DR config is {}", request.body().asJson());
    DrConfigSetTablesForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigSetTablesForm.class);
    formData.tables = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.tables);
    if (Objects.nonNull(formData.bootstrapParams)) {
      validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);
    }
    return formData;
  }

  private DrConfigSetDatabasesForm parseSetDatabasesForm(UUID customerUUID, Http.Request request) {
    log.debug("Request body to set database a DR config is {}", request.body().asJson());
    DrConfigSetDatabasesForm formData =
        formFactory.getFormDataOrBadRequest(
            request.body().asJson(), DrConfigSetDatabasesForm.class);
    formData.databases = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.databases);
    return formData;
  }

  private DrConfigRestartForm parseRestartForm(UUID customerUUID, Http.Request request) {
    log.debug("Request body to restart a DR config is {}", request.body().asJson());
    DrConfigRestartForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigRestartForm.class);
    formData.dbs = XClusterConfigTaskBase.convertUuidStringsToIdStringSet(formData.dbs);
    if (Objects.nonNull(formData.bootstrapParams)) {
      validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);
    }
    return formData;
  }

  private DrConfigReplaceReplicaForm parseReplaceReplicaForm(
      UUID customerUUID, Universe sourceUniverse, Universe targetUniverse, Http.Request request) {
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
      validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);
    }
    return formData;
  }

  private DrConfigSwitchoverForm parseSwitchoverForm(Http.Request request) {
    log.debug("Request body to switchover a DR config is {}", request.body().asJson());
    DrConfigSwitchoverForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigSwitchoverForm.class);

    if (formData.primaryUniverseUuid.equals(formData.drReplicaUniverseUuid)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "primaryUniverseUuid cannot be the same as drReplicaUniverseUuid");
    }

    return formData;
  }

  private DrConfigFailoverForm parseFailoverForm(Http.Request request) {
    log.debug("Request body to failover a DR config is {}", request.body().asJson());
    DrConfigFailoverForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigFailoverForm.class);

    if (formData.primaryUniverseUuid.equals(formData.drReplicaUniverseUuid)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "primaryUniverseUuid cannot be the same as drReplicaUniverseUuid");
    }

    return formData;
  }

  private void validateBackupRequestParamsForBootstrapping(
      BootstrapParams.BootstrapBackupParams bootstrapBackupParams, UUID customerUUID) {
    XClusterConfigTaskBase.validateBackupRequestParamsForBootstrapping(
        customerConfigService, backupHelper, bootstrapBackupParams, customerUUID);
  }

  private static BootstrapParams getBootstrapParamsFromRestartBootstrapParams(
      @Nullable XClusterConfigRestartFormData.RestartBootstrapParams restartBootstrapParams,
      Set<String> tableIds) {
    if (Objects.isNull(restartBootstrapParams)) {
      return null;
    }
    BootstrapParams bootstrapParams = new BootstrapParams();
    bootstrapParams.tables = tableIds;
    bootstrapParams.backupRequestParams = restartBootstrapParams.backupRequestParams;
    bootstrapParams.allowBootstrap = true;
    return bootstrapParams;
  }

  /**
   * It runs some pre-checks to ensure that the reverse direction xCluster config can be set up. A
   * reverse direction xCluster config is almost the same as the main xCluster config but in the
   * reverse direction.
   *
   * @param taskType This specifies the task that triggered the creation of a reverse direction
   *     xCluster config.
   * @param requestedTableInfoList The table info list on the target universe that will be part of
   *     the failover xCluster config
   * @param targetTableInfoList The table info list for all tables on the target universe
   * @param targetUniverse The target universe in the main xCluster config which is the same as the
   *     source universe in the reverse direction xCluster config
   * @param sourceUniverse The source universe in the main xCluster config which is the same as the
   *     target universe in the reverse direction xCluster config
   */
  public static void drSwitchoverFailoverPreChecks(
      CustomerTask.TaskType taskType,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList,
      Universe targetUniverse,
      Universe sourceUniverse) {
    Set<String> tableIds = XClusterConfigTaskBase.getTableIds(requestedTableInfoList);

    // General xCluster pre-checks.
    XClusterConfigTaskBase.verifyTablesNotInReplication(
        tableIds, targetUniverse.getUniverseUUID(), sourceUniverse.getUniverseUUID());
    XClusterConfigController.certsForCdcDirGFlagCheck(targetUniverse, sourceUniverse);

    // If table type is YSQL, all tables in that keyspace are selected.
    if (XClusterConfigTaskBase.getTableType(requestedTableInfoList)
        == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      XClusterConfigTaskBase.groupByNamespaceId(requestedTableInfoList)
          .forEach(
              (namespaceId, tablesInfoList) -> {
                Set<String> requestedTableIdsInNamespace =
                    XClusterConfigTaskBase.getTableIds(tablesInfoList).stream()
                        .filter(tableIds::contains)
                        .collect(Collectors.toSet());
                if (!requestedTableIdsInNamespace.isEmpty()) {
                  Set<String> tableIdsInNamespace =
                      targetTableInfoList.stream()
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
                  if (tableIdsInNamespace.size() > requestedTableIdsInNamespace.size()) {
                    Set<String> extraTableIds =
                        tableIdsInNamespace.stream()
                            .filter(tableId -> !requestedTableIdsInNamespace.contains(tableId))
                            .collect(Collectors.toSet());
                    throw new IllegalArgumentException(
                        String.format(
                            "The DR replica databases under replication contain tables which are"
                                + " not part of the DR config. %s is not possible until the extra"
                                + " tables on the DR replica are removed. The extra tables from DR"
                                + " replica are: %s",
                            taskType, extraTableIds));
                  }
                  if (tableIdsInNamespace.size() < requestedTableIdsInNamespace.size()) {
                    Set<String> extraTableIds =
                        requestedTableIdsInNamespace.stream()
                            .filter(tableId -> !tableIdsInNamespace.contains(tableId))
                            .collect(Collectors.toSet());
                    throw new IllegalArgumentException(
                        String.format(
                            "The DR replica databases under replication dropped tables that are"
                                + " part of the DR config. %s is not possible until the same tables"
                                + " are dropped from the DR primary and DR config. The extra tables"
                                + " from DR config are: %s",
                            taskType, extraTableIds));
                  }
                }
              });
    }
  }

  private void disallowActionOnErrorState(DrConfig drConfig) {
    if (drConfig.getState() == State.Error) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "DR config: %s is in error state and cannot run current action", drConfig.getName()));
    }
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
}
