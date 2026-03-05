package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.XClusterScheduler;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.XClusterCreatePrecheck;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.common.dr.DrConfigHelper.DrConfigTaskResult;
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
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
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
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
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
    DrConfigRestartForm restartForm = parseRestartForm(customerUUID, request);

    UUID taskUUID =
        drConfigHelper.restartDrConfigTask(customerUUID, drConfigUuid, restartForm, isForceDelete);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUuid.toString(),
            ActionType.Restart,
            Json.toJson(restartForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUuid).asResult();
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
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);
    // Parse and validate request.
    DrConfigReplaceReplicaForm replaceReplicaForm =
        parseReplaceReplicaForm(customerUUID, sourceUniverse, targetUniverse, request);
    if (replaceReplicaForm.bootstrapParams == null) {
      replaceReplicaForm.bootstrapParams = drConfig.getBootstrapBackupParams();
    }
    UUID taskUUID =
        drConfigHelper.replaceReplicaTask(
            customer, drConfig, xClusterConfig, sourceUniverse, targetUniverse, replaceReplicaForm);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUuid.toString(),
            ActionType.Edit,
            Json.toJson(replaceReplicaForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUuid).asResult();
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

    DrConfigSwitchoverForm switchoverForm = parseSwitchoverForm(request);
    UUID taskUUID =
        drConfigHelper.switchoverDrConfigTask(customerUUID, drConfigUuid, switchoverForm);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUuid.toString(),
            ActionType.Switchover,
            Json.toJson(switchoverForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUuid).asResult();
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
    DrConfigFailoverForm failoverForm = parseFailoverForm(request);
    UUID taskUUID = drConfigHelper.failoverDrConfigTask(customerUUID, drConfigUuid, failoverForm);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.DrConfig,
            drConfigUuid.toString(),
            ActionType.Failover,
            Json.toJson(failoverForm),
            taskUUID);
    return new YBPTask(taskUUID, drConfigUuid).asResult();
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
    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.status = taskType == CustomerTask.TaskType.Resume ? "Running" : "Paused";
    UUID taskUUID =
        drConfigHelper.toggleDrState(customerUUID, drConfigUUID, editFormData, taskType);

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
