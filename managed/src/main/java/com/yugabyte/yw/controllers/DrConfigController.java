package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams;
import com.yugabyte.yw.forms.XClusterConfigGetResp;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Audit.TargetType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Disaster Recovery",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
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
  public Result create(UUID customerUUID, Http.Request request) {
    log.info("Received create drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfigCreateForm createForm = parseCreateForm(customerUUID, request);
    Universe sourceUniverse = Universe.getOrBadRequest(createForm.sourceUniverseUUID, customer);
    Universe targetUniverse = Universe.getOrBadRequest(createForm.targetUniverseUUID, customer);

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
   * API that gets a disaster recovery configuration.
   *
   * @return A form representing the requested dr config
   */
  @ApiOperation(
      nickname = "getDrConfig",
      value = "Get disaster recovery config",
      response = XClusterConfigGetResp.class)
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
   * API that deletes an disaster recovery configuration.
   *
   * @return An instance of {@link YBPTask} indicating whether the task was created successfully
   */
  @ApiOperation(
      nickname = "deleteXClusterConfig",
      value = "Delete xcluster config",
      response = YBPTask.class)
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

    validateBackupRequestParamsForBootstrapping(formData.bootstrapBackupParams, customerUUID);

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
}
