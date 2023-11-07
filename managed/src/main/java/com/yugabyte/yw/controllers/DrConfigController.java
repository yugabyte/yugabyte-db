package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigEditForm;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigFailoverForm.Type;
import com.yugabyte.yw.forms.DrConfigSafetimeResp;
import com.yugabyte.yw.forms.DrConfigSafetimeResp.NamespaceSafetime;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams;
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
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.NamespaceSafeTimePB;
import org.yb.master.MasterTypes.RelationType;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Disaster Recovery",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH),
    hidden = true)
@Slf4j
public class DrConfigController extends AuthenticatedController {

  private final Commissioner commissioner;
  private final BackupHelper backupHelper;
  private final CustomerConfigService customerConfigService;
  private final YBClientService ybService;
  private final RuntimeConfGetter confGetter;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  public DrConfigController(
      Commissioner commissioner,
      BackupHelper backupHelper,
      CustomerConfigService customerConfigService,
      YBClientService ybService,
      RuntimeConfGetter confGetter,
      XClusterUniverseService xClusterUniverseService) {
    this.commissioner = commissioner;
    this.backupHelper = backupHelper;
    this.customerConfigService = customerConfigService;
    this.ybService = ybService;
    this.confGetter = confGetter;
    this.xClusterUniverseService = xClusterUniverseService;
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
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
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

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(
            ybService, sourceUniverse, true /* excludeSystemTables */);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        getRequestedTableInfoList(createForm.dbs, sourceTableInfoList);

    Set<String> tableIds = XClusterConfigTaskBase.getTableIds(requestedTableInfoList);
    CommonTypes.TableType tableType = XClusterConfigTaskBase.getTableType(requestedTableInfoList);
    Map<String, List<String>> mainTableIndexTablesMap =
        XClusterConfigTaskBase.getMainTableIndexTablesMap(this.ybService, sourceUniverse, tableIds);

    XClusterConfigController.xClusterCreatePreChecks(
        tableIds, tableType, ConfigType.Txn, sourceUniverse, targetUniverse, confGetter);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(
            ybService, targetUniverse, true /* excludeSystemTables */);
    Map<String, String> sourceTableIdTargetTableIdMap =
        XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
            requestedTableInfoList, targetTableInfoList);

    BootstrapParams bootstrapParams =
        getBootstrapParamsFromBootstarpBackupParams(createForm.bootstrapBackupParams, tableIds);
    XClusterConfigController.xClusterBootstrappingPreChecks(
        requestedTableInfoList,
        sourceTableInfoList,
        targetUniverse,
        sourceTableIdTargetTableIdMap,
        ybService,
        bootstrapParams,
        null /* currentReplicationGroupName */);

    // Todo: Ensure the PITR parameters have the right RPOs.

    if (createForm.dryRun) {
      return YBPSuccess.withMessage("The pre-checks are successful");
    }

    // Create xCluster config object.
    DrConfig drConfig =
        DrConfig.create(
            createForm.name,
            createForm.sourceUniverseUUID,
            createForm.targetUniverseUUID,
            tableIds);
    drConfig
        .getActiveXClusterConfig()
        .updateIndexTablesFromMainTableIndexTablesMap(mainTableIndexTablesMap);

    // Submit task to set up xCluster config.
    DrConfigTaskParams taskParams =
        new DrConfigTaskParams(
            drConfig,
            bootstrapParams,
            requestedTableInfoList,
            mainTableIndexTablesMap,
            sourceTableIdTargetTableIdMap,
            createForm.pitrParams);
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

  /**
   * API that edits a disaster recovery configuration.
   *
   * @return An instance of YBPTask including the task uuid that is editing the dr config
   */
  @ApiOperation(
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
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result edit(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received edit drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    DrConfigEditForm editForm =
        parseEditForm(customerUUID, sourceUniverse, targetUniverse, request);
    Universe newTargetUniverse = Universe.getOrBadRequest(editForm.newTargetUniverseUUID, customer);

    // Todo: lift this restriction.
    if (!xClusterConfig.getStatus().equals(XClusterConfigStatusType.Running)
        || !xClusterConfig.getTableDetails().stream()
            .map(XClusterTableConfig::getStatus)
            .allMatch(tableConfigStatus -> tableConfigStatus.equals(Status.Running))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "the underlying xCluster config and all of its replication streams must "
              + "be running status.");
    }

    Set<String> tableIds = xClusterConfig.getTableIdsWithReplicationSetup();

    // Add index tables.
    Map<String, List<String>> mainTableIndexTablesMap =
        XClusterConfigTaskBase.getMainTableIndexTablesMap(this.ybService, sourceUniverse, tableIds);
    Set<String> indexTableIdSet =
        mainTableIndexTablesMap.values().stream().flatMap(List::stream).collect(Collectors.toSet());
    tableIds.addAll(indexTableIdSet);

    log.debug("tableIds are {}", tableIds);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(
            ybService, sourceUniverse, true /* excludeSystemTables */);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.filterTableInfoListByTableIds(sourceTableInfoList, tableIds);

    XClusterConfigTaskBase.verifyTablesNotInReplication(
        tableIds, sourceUniverse.getUniverseUUID(), newTargetUniverse.getUniverseUUID());
    XClusterConfigController.certsForCdcDirGFlagCheck(sourceUniverse, newTargetUniverse);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> newTargetTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(
            ybService, newTargetUniverse, true /* excludeSystemTables */);
    Map<String, String> sourceTableIdNewTargetTableIdMap =
        XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
            requestedTableInfoList, newTargetTableInfoList);

    BootstrapParams bootstrapParams =
        getBootstrapParamsFromBootstarpBackupParams(editForm.bootstrapBackupParams, tableIds);
    XClusterConfigController.xClusterBootstrappingPreChecks(
        requestedTableInfoList,
        sourceTableInfoList,
        newTargetUniverse,
        sourceTableIdNewTargetTableIdMap,
        ybService,
        bootstrapParams,
        null /* currentReplicationGroupName */);

    // Todo: add a dryRun option here.

    // Create xCluster config object.
    XClusterConfig newTargetXClusterConfig =
        drConfig.addXClusterConfig(
            sourceUniverse.getUniverseUUID(), newTargetUniverse.getUniverseUUID());
    newTargetXClusterConfig.updateTables(tableIds, tableIds /* tableIdsNeedBootstrap */);
    newTargetXClusterConfig.updateIndexTablesFromMainTableIndexTablesMap(mainTableIndexTablesMap);
    newTargetXClusterConfig.setSecondary(true);

    // Submit task to set up xCluster config.
    DrConfigTaskParams taskParams =
        new DrConfigTaskParams(
            drConfig,
            null /* failoverType */,
            null /* safetimeEpochUs */,
            newTargetXClusterConfig,
            bootstrapParams,
            requestedTableInfoList,
            mainTableIndexTablesMap,
            sourceTableIdNewTargetTableIdMap);
    UUID taskUUID = commissioner.submit(TaskType.EditDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Edit,
        drConfig.getName());

    log.info("Submitted edit DrConfig({}), task {}", drConfig.getUuid(), taskUUID);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.DrConfig,
            drConfig.getUuid().toString(),
            Audit.ActionType.Edit,
            Json.toJson(editForm),
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
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result failover(UUID customerUUID, UUID drConfigUuid, Http.Request request) {
    log.info("Received failover drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfigFailoverForm failoverForm = parseFailoverForm(request);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    // To do planned failover, the xCluster config and all the tables in that config must be in
    // the green status because we are going to drop that config and the information for bad
    // replication streams will be lost.
    if (failoverForm.type.equals(Type.PLANNED)
        && (!xClusterConfig.getStatus().equals(XClusterConfigStatusType.Running)
            || !xClusterConfig.getTableDetails().stream()
                .map(XClusterTableConfig::getStatus)
                .allMatch(tableConfigStatus -> tableConfigStatus.equals(Status.Running)))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "In order to do a planned failover, the underlying xCluster config and all of its "
              + "replication streams must be running status. Please either restart the config "
              + "to put everything is working status, or if the xCluster config is in Running "
              + "status, you can remove the tables whose replication is broken to run planned "
              + "failover.");
    }

    // Todo: PLAT-10130, handle cases where the planned failover task fails.
    // Todo: Add pre-checks for user's input safetime.

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(
            ybService, targetUniverse, true /* excludeSystemTables */);

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
              "The following tables are in replication with no corresponding table on the "
                  + "target universe: %s. This can happen if the table is dropped without being "
                  + "removed from replication first. You may fix this issue by running `Reconcile "
                  + "config with DB` from UI",
              sourceTableIdsWithNoTableOnTargetUniverse));
    }
    // Use table IDs on the target universe for failover xCluster.
    Set<String> tableIds = new HashSet<>(sourceTableIdTargetTableIdMap.values());
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.filterTableInfoListByTableIds(targetTableInfoList, tableIds);

    failoverXClusterCreatePreChecks(
        requestedTableInfoList, targetTableInfoList, targetUniverse, sourceUniverse);

    Map<String, String> targetTableIdSourceTableIdMap =
        sourceTableIdTargetTableIdMap.entrySet().stream()
            .collect(Collectors.toMap(Entry::getValue, Entry::getKey));
    Map<String, List<String>> mainTableIndexTablesMap =
        XClusterConfigTaskBase.getMainTableIndexTablesMap(this.ybService, targetUniverse, tableIds);

    // Make sure the safetime for all the namespaces is specified.
    if (failoverForm.type.equals(Type.UNPLANNED)) {
      Set<String> namespaceIdsWithSafetime = failoverForm.namespaceIdSafetimeEpochUsMap.keySet();
      Set<String> namespaceIdsWithoutSafetime =
          XClusterConfigTaskBase.getNamespaces(requestedTableInfoList).stream()
              .map(namespace -> namespace.getId().toStringUtf8())
              .filter(namespaceId -> !namespaceIdsWithSafetime.contains(namespaceId))
              .collect(Collectors.toSet());
      if (!namespaceIdsWithoutSafetime.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Safetime must be specified for all the databases in the disaster recovery "
                    + "config; namespaces ids without safetimes: %s",
                namespaceIdsWithoutSafetime));
      }
    }

    XClusterConfig failoverXClusterConfig =
        drConfig.addXClusterConfig(
            xClusterConfig.getTargetUniverseUUID(), xClusterConfig.getSourceUniverseUUID());
    failoverXClusterConfig.updateTables(tableIds, null /* tableIdsNeedBootstrap */);
    failoverXClusterConfig.updateIndexTablesFromMainTableIndexTablesMap(mainTableIndexTablesMap);
    failoverXClusterConfig.setSecondary(true);

    // Submit task to set up xCluster config.
    DrConfigTaskParams taskParams =
        new DrConfigTaskParams(
            drConfig,
            failoverForm.type,
            failoverForm.namespaceIdSafetimeEpochUsMap,
            failoverXClusterConfig,
            null /* bootstrapParams */,
            requestedTableInfoList,
            mainTableIndexTablesMap,
            targetTableIdSourceTableIdMap);
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
      response = DrConfig.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result get(UUID customerUUID, UUID drUUID) {
    log.info("Received get DrConfig({}) request", drUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drUUID);

    for (XClusterConfig xClusterConfig : drConfig.getXClusterConfigs()) {
      XClusterConfigTaskBase.setReplicationStatus(this.xClusterUniverseService, xClusterConfig);
    }

    return PlatformResults.withData(drConfig);
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
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
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
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getSafetime(UUID customerUUID, UUID drUUID) {
    log.info("Received getSafetime DrConfig({}) request", drUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drUUID);

    List<NamespaceSafeTimePB> namespaceSafeTimeList =
        xClusterUniverseService.getNamespaceSafeTimeList(drConfig.getActiveXClusterConfig());

    DrConfigSafetimeResp safetimeResp = new DrConfigSafetimeResp();
    namespaceSafeTimeList.forEach(
        namespaceSafeTimePB ->
            safetimeResp.safetimes.add(new NamespaceSafetime(namespaceSafeTimePB)));
    return PlatformResults.withData(safetimeResp);
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
    validateBackupRequestParamsForBootstrapping(formData.bootstrapBackupParams, customerUUID);

    return formData;
  }

  private DrConfigEditForm parseEditForm(
      UUID customerUUID, Universe sourceUniverse, Universe targetUniverse, Http.Request request) {
    log.debug("Request body to edit an DR config is {}", request.body().asJson());
    DrConfigEditForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigEditForm.class);

    if (Objects.isNull(formData.newTargetUniverseUUID)) {
      throw new IllegalArgumentException("No change to the dr config detected");
    } else {
      if (Objects.isNull(formData.bootstrapBackupParams)) {
        throw new IllegalArgumentException(
            "bootstrapBackupParams cannot be null if you are changing the standby universe");
      }
    }

    if (targetUniverse.getUniverseUUID().equals(formData.newTargetUniverseUUID)) {
      throw new IllegalArgumentException(
          "No change to the dr config detected; newTargetUniverseUUID is the same as the "
              + "current standby universe");
    }

    if (Objects.equals(sourceUniverse.getUniverseUUID(), formData.newTargetUniverseUUID)) {
      throw new IllegalArgumentException(
          "newTargetUniverseUUID cannot be the same as the current primary universe");
    }

    validateBackupRequestParamsForBootstrapping(formData.bootstrapBackupParams, customerUUID);

    return formData;
  }

  private DrConfigFailoverForm parseFailoverForm(Http.Request request) {
    log.debug("Request body to failover a DR config is {}", request.body().asJson());
    DrConfigFailoverForm formData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), DrConfigFailoverForm.class);

    if (formData.type.equals(Type.UNPLANNED)
        && Objects.isNull(formData.namespaceIdSafetimeEpochUsMap)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "To do an unplanned failover, namespaceIdSafetimeEpochUsMap "
              + "in the request is required");
    }

    return formData;
  }

  private void validateBackupRequestParamsForBootstrapping(
      XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams bootstarpBackupParams,
      UUID customerUUID) {
    XClusterConfigTaskBase.validateBackupRequestParamsForBootstrapping(
        customerConfigService, backupHelper, bootstarpBackupParams, customerUUID);
  }

  private List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getRequestedTableInfoList(
      Set<String> dbIds,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList) {
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        sourceTableInfoList.stream()
            .filter(tableInfo -> dbIds.contains(tableInfo.getNamespace().getId().toStringUtf8()))
            .collect(Collectors.toList());
    Set<String> foundDBIds =
        requestedTableInfoList.stream()
            .map(tableInfo -> tableInfo.getNamespace().getName())
            .collect(Collectors.toSet());
    // Ensure all DB names are found.
    if (foundDBIds.size() != dbIds.size()) {
      Set<String> missingDbIds =
          dbIds.stream()
              .filter(tableId -> !foundDBIds.contains(tableId))
              .collect(Collectors.toSet());
      throw new IllegalArgumentException(
          String.format(
              "Some of the DB ids were not found: was %d, found %d, missing dbs: %s",
              dbIds.size(), foundDBIds.size(), missingDbIds));
    }
    return requestedTableInfoList;
  }

  private static BootstrapParams getBootstrapParamsFromBootstarpBackupParams(
      BootstarpBackupParams bootstrapBackupParams, Set<String> tableIds) {
    BootstrapParams bootstrapParams = new BootstrapParams();
    bootstrapParams.tables = tableIds;
    bootstrapParams.backupRequestParams = bootstrapBackupParams;
    return bootstrapParams;
  }

  /**
   * It runs some pre-checks to ensure that the failover xClsuter config can be set up. A failover
   * xCluster config is almost the same as the main xCluster config but in the reverse direction.
   *
   * @param requestedTableInfoList The table info list on the target universe that will be part of
   *     the failover xCluster config
   * @param targetTableInfoList The table info list for all tables on the target universe
   * @param targetUniverse The target universe in the main xCluster config which is the same as the
   *     source universe in the failover xCluster config
   * @param sourceUniverse The source universe in the main xCluster config which is the same as the
   *     target universe in the failover xCluster config
   */
  public static void failoverXClusterCreatePreChecks(
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
                                  !tableInfo
                                          .getRelationType()
                                          .equals(RelationType.SYSTEM_TABLE_RELATION)
                                      && tableInfo
                                          .getNamespace()
                                          .getId()
                                          .toStringUtf8()
                                          .equals(namespaceId))
                          .map(tableInfo -> tableInfo.getId().toStringUtf8())
                          .collect(Collectors.toSet());
                  if (tableIdsInNamespace.size() != requestedTableIdsInNamespace.size()) {
                    throw new IllegalArgumentException(
                        String.format(
                            "For YSQL tables, all the tables in a keyspace must be selected: "
                                + "selected: %s, tables in the keyspace: %s",
                            requestedTableIdsInNamespace, tableIdsInNamespace));
                  }
                }
              });
    }
  }
}
