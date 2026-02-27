// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.dr;

import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getRequestedTableInfoList;
import static org.apache.commons.validator.routines.UrlValidator.ALLOW_LOCAL_URLS;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.XClusterScheduler;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.XClusterCreatePrecheck;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.XClusterUtil;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.controllers.XClusterConfigController;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.DrConfigEditForm;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigReplaceReplicaForm;
import com.yugabyte.yw.forms.DrConfigRestartForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
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
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.validator.routines.UrlValidator;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetUniverseReplicationInfoResponse;
import org.yb.client.GetXClusterOutboundReplicationGroupInfoResponse;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.*;

@Slf4j
@Singleton
public class DrConfigHelper {

  private static final int maxRetryCount = 5;
  private final Commissioner commissioner;
  private final YBClientService ybService;
  private final RuntimeConfGetter confGetter;
  private final AutoFlagUtil autoFlagUtil;
  private final SoftwareUpgradeHelper softwareUpgradeHelper;
  private final XClusterCreatePrecheck xClusterCreatePrecheck;
  private final CustomerConfigService customerConfigService;
  private final BackupHelper backupHelper;
  private final XClusterUniverseService xClusterUniverseService;
  private final XClusterScheduler xClusterScheduler;
  private final UniverseTableHandler tableHandler;

  public record DrConfigTaskResult(UUID drConfigUuid, UUID taskUuid, String message) {}

  @Inject
  public DrConfigHelper(
      RuntimeConfGetter confGetter,
      Commissioner commissioner,
      SoftwareUpgradeHelper softwareUpgradeHelper,
      XClusterCreatePrecheck xClusterCreatePrecheck,
      AutoFlagUtil autoFlagUtil,
      YBClientService ybService,
      BackupHelper backupHelper,
      XClusterScheduler xClusterScheduler,
      UniverseTableHandler tableHandler,
      CustomerConfigService customerConfigService,
      XClusterUniverseService xClusterUniverseService) {
    this.confGetter = confGetter;
    this.commissioner = commissioner;
    this.softwareUpgradeHelper = softwareUpgradeHelper;
    this.xClusterCreatePrecheck = xClusterCreatePrecheck;
    this.autoFlagUtil = autoFlagUtil;
    this.ybService = ybService;
    this.backupHelper = backupHelper;
    this.xClusterScheduler = xClusterScheduler;
    this.tableHandler = tableHandler;
    this.customerConfigService = customerConfigService;
    this.xClusterUniverseService = xClusterUniverseService;
  }

  public boolean abortDrConfigTask(UUID taskUUID) {
    return commissioner.abortTask(taskUUID, false);
  }

  public DrConfigTaskResult createDrConfigTask(UUID customerUUID, DrConfigCreateForm createForm) {

    Customer customer = Customer.getOrBadRequest(customerUUID);
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
        confGetter.getConfForScope(
            sourceUniverse, UniverseConfKeys.dbScopedXClusterCreationEnabled);

    if (Objects.isNull(createForm.pitrParams)) {
      createForm.pitrParams = new PitrParams();
      createForm.pitrParams.retentionPeriodSec =
          confGetter
              .getConfForScope(
                  targetUniverse, UniverseConfKeys.txnXClusterPitrDefaultRetentionPeriod)
              .getSeconds();
      createForm.pitrParams.snapshotIntervalSec =
          Math.min(
              createForm.pitrParams.retentionPeriodSec - 1,
              confGetter
                  .getConfForScope(
                      targetUniverse, UniverseConfKeys.txnXClusterPitrDefaultSnapshotInterval)
                  .getSeconds());
    } else if (createForm.pitrParams.snapshotIntervalSec == 0L) {
      createForm.pitrParams.snapshotIntervalSec =
          Math.min(
              createForm.pitrParams.retentionPeriodSec - 1,
              confGetter
                  .getConfForScope(
                      targetUniverse, UniverseConfKeys.txnXClusterPitrDefaultSnapshotInterval)
                  .getSeconds());
    }

    validatePitrParams(createForm.pitrParams);

    List<TableInfo> sourceTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, sourceUniverse);

    List<TableInfo> requestedTableInfoList =
        getRequestedTableInfoList(createForm.dbs, sourceTableInfoList);

    List<TableInfo> targetTableInfoList =
        XClusterConfigTaskBase.getTableInfoList(ybService, targetUniverse);

    xClusterCreatePrecheck.xClusterCreatePreChecks(
        requestedTableInfoList,
        isDbScoped ? ConfigType.Db : ConfigType.Txn,
        sourceUniverse,
        sourceTableInfoList,
        targetUniverse,
        targetTableInfoList);

    Set<String> tableIds = XClusterConfigTaskBase.getTableIds(requestedTableInfoList);
    BootstrapParams bootstrapParams =
        getBootstrapParamsFromRestartBootstrapParams(createForm.bootstrapParams, tableIds);

    Map<String, String> sourceTableIdTargetTableIdMap =
        XClusterConfigTaskBase.getSourceTableIdTargetTableIdMap(
            requestedTableInfoList, targetTableInfoList);

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
      return new DrConfigTaskResult(null, null, "The pre-checks are successful");
    }

    DrConfig drConfig;
    DrConfigTaskParams taskParams;

    if (isDbScoped) {
      // Automatic DDL mode is enabled if the corresponding universe conf is set to true and the
      // participating universes have the minimum required version.
      boolean isAutomaticDdlMode =
          confGetter.getConfForScope(
                  sourceUniverse, UniverseConfKeys.XClusterDbScopedAutomaticDdlCreationEnabled)
              && XClusterUtil.supportsAutomaticDdl(sourceUniverse)
              && XClusterUtil.supportsAutomaticDdl(targetUniverse);

      drConfig =
          DrConfig.create(
              createForm.name,
              createForm.sourceUniverseUUID,
              createForm.targetUniverseUUID,
              createForm.bootstrapParams.backupRequestParams,
              createForm.pitrParams,
              createForm.dbs,
              isAutomaticDdlMode);

      if (createForm.getKubernetesResourceDetails() != null) {
        drConfig.setKubernetesResourceDetails(createForm.getKubernetesResourceDetails());
      }

      taskParams =
          new DrConfigTaskParams(
              drConfig,
              getBootstrapParamsFromRestartBootstrapParams(
                  createForm.bootstrapParams, new HashSet<>()),
              createForm.dbs,
              createForm.pitrParams);
    } else {

      Map<String, List<String>> mainTableIndexTablesMap =
          XClusterConfigTaskBase.getMainTableIndexTablesMap(ybService, sourceUniverse, tableIds);

      // Create xCluster config object.
      drConfig =
          DrConfig.create(
              createForm.name,
              createForm.sourceUniverseUUID,
              createForm.targetUniverseUUID,
              tableIds,
              createForm.bootstrapParams.backupRequestParams,
              createForm.pitrParams);
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
    return new DrConfigTaskResult(drConfig.getUuid(), taskUUID, null);
  }

  public void waitForTask(UUID taskUUID) throws InterruptedException {
    commissioner.waitForTask(taskUUID, null);
  }

  public UUID editDrConfigTask(UUID customerUUID, UUID drConfigUuid, DrConfigEditForm editForm) {
    log.info("Received edit drConfig request");

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);

    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    XClusterUtil.ensureUpgradeIsComplete(sourceUniverse, targetUniverse);
    validateEditForm(editForm, customer.getUuid(), drConfig);
    DrConfigTaskParams taskParams =
        new DrConfigTaskParams(
            drConfig, editForm.bootstrapParams, editForm.pitrParams, editForm.webhookUrls);

    UUID taskUUID = commissioner.submit(TaskType.EditDrConfigParams, taskParams);
    CustomerTask.create(
        customer,
        Objects.isNull(drConfig.getActiveXClusterConfig())
            ? drConfig.getUuid()
            : drConfig.getActiveXClusterConfig().getSourceUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Edit,
        drConfig.getName());

    log.info("Submitted edit DrConfig({}), task {}", drConfig.getUuid(), taskUUID);
    return taskUUID;
  }

  public UUID deleteDrConfigTask(UUID customerUUID, UUID drConfigUuid, boolean isForceDelete) {
    log.info(
        "Received delete drConfig({}) request with isForceDelete={}", drConfigUuid, isForceDelete);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    verifyTaskAllowed(drConfig, TaskType.DeleteDrConfig);
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
    return taskUUID;
  }

  public UUID setDatabasesTask(
      UUID customerUUID, UUID drConfigUuid, DrConfigSetDatabasesForm setDatabasesForm) {
    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    if (setDatabasesForm.getKubernetesResourceDetails() != null) {
      drConfig.setKubernetesResourceDetails(setDatabasesForm.getKubernetesResourceDetails());
    }
    verifyTaskAllowed(drConfig, TaskType.SetDatabasesDrConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID(), customer);
    Universe targetUniverse =
        Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID(), customer);

    XClusterUtil.ensureUpgradeIsComplete(sourceUniverse, targetUniverse);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkSourcePromotedAutoFlagsPromotedOnTarget(sourceUniverse, targetUniverse);
    }
    if (xClusterConfig.getType() != ConfigType.Db) {
      throw new PlatformServiceException(
          BAD_REQUEST, "This operation is only supported for db-scoped xCluster configs.");
    }

    Set<String> existingDatabaseIds = xClusterConfig.getDbIds();
    Set<String> newDatabaseIds = setDatabasesForm.dbs;
    Set<String> databaseIdsToAdd = Sets.difference(newDatabaseIds, existingDatabaseIds);
    Set<String> databaseIdsToRemove = Sets.difference(existingDatabaseIds, newDatabaseIds);

    if (databaseIdsToAdd.isEmpty() && databaseIdsToRemove.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "The list of new databases to add/remove is empty.");
    }

    XClusterUtil.checkDbScopedNonEmptyDbs(newDatabaseIds);
    XClusterConfigController.verifyTaskAllowed(xClusterConfig, TaskType.EditXClusterConfig);
    XClusterConfigTaskParams taskParams =
        XClusterConfigController.getSetDatabasesTaskParams(
            xClusterConfig, newDatabaseIds, databaseIdsToAdd, databaseIdsToRemove);

    UUID taskUUID = commissioner.submit(TaskType.SetDatabasesDrConfig, taskParams);
    CustomerTask.create(
        customer,
        sourceUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.DrConfig,
        CustomerTask.TaskType.Edit,
        drConfig.getName());
    log.info("Submitted set databases DrConfig({}), task {}", drConfig.getUuid(), taskUUID);
    return taskUUID;
  }

  public UUID replaceReplicaTask(
      Customer customer,
      DrConfig drConfig,
      XClusterConfig xClusterConfig,
      Universe sourceUniverse,
      Universe targetUniverse,
      DrConfigReplaceReplicaForm replaceReplicaForm) {

    Universe newTargetUniverse =
        Universe.getOrBadRequest(replaceReplicaForm.drReplicaUniverseUuid, customer);

    if (confGetter.getGlobalConf(GlobalConfKeys.xclusterEnableAutoFlagValidation)) {
      autoFlagUtil.checkPromotedAutoFlagsEquality(sourceUniverse, newTargetUniverse);
    }
    if (replaceReplicaForm.getKubernetesResourceDetails() != null) {
      drConfig.setKubernetesResourceDetails(replaceReplicaForm.getKubernetesResourceDetails());
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
    return taskUUID;
  }

  public UUID restartDrConfigTask(
      UUID customerUUID,
      UUID drConfigUuid,
      DrConfigRestartForm restartForm,
      boolean isForceDelete) {
    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    if (restartForm.getKubernetesResourceDetails() != null) {
      drConfig.setKubernetesResourceDetails(restartForm.getKubernetesResourceDetails());
    }
    verifyTaskAllowed(drConfig, TaskType.RestartDrConfig);

    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();

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
    return taskUUID;
  }

  public UUID switchoverDrConfigTask(
      UUID customerUUID, UUID drConfigUuid, DrConfigSwitchoverForm switchoverForm) {
    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    if (switchoverForm.getKubernetesResourceDetails() != null) {
      drConfig.setKubernetesResourceDetails(switchoverForm.getKubernetesResourceDetails());
    }
    verifyTaskAllowed(drConfig, TaskType.SwitchoverDrConfig);
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
    return taskUUID;
  }

  public UUID failoverDrConfigTask(
      UUID customerUUID, UUID drConfigUuid, DrConfigFailoverForm failoverForm) {
    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUuid);
    if (failoverForm.getKubernetesResourceDetails() != null) {
      drConfig.setKubernetesResourceDetails(failoverForm.getKubernetesResourceDetails());
    }
    verifyTaskAllowed(drConfig, TaskType.FailoverDrConfig);
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
    return taskUUID;
  }

  public UUID toggleDrState(
      UUID customerUUID,
      UUID drConfigUUID,
      XClusterConfigEditFormData editFormData,
      CustomerTask.TaskType taskType) {
    String operation = taskType == CustomerTask.TaskType.Resume ? "resume" : "pause";
    log.info("Received {} DrConfig({}) request", operation, drConfigUUID);

    // Parse and validate request.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    DrConfig drConfig = DrConfig.getValidConfigOrBadRequest(customer, drConfigUUID);
    if (editFormData.getKubernetesResourceDetails() != null) {
      drConfig.setKubernetesResourceDetails(editFormData.getKubernetesResourceDetails());
    }
    verifyTaskAllowed(drConfig, TaskType.EditXClusterConfig);
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
    return taskUUID;
  }

  public void verifyTaskAllowed(DrConfig drConfig, TaskType taskType) {
    if (!XClusterConfigTaskBase.isTaskAllowed(drConfig, taskType)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s task is not allowed; with state `%s`, the allowed tasks are %s",
              taskType, drConfig.getState(), XClusterConfigTaskBase.getAllowedTasks(drConfig)));
    }
  }

  private void validateEditForm(DrConfigEditForm formData, UUID customerUUID, DrConfig drConfig) {
    boolean changeInParams = false;

    if (formData.bootstrapParams != null) {
      validateBackupRequestParamsForBootstrapping(
          formData.bootstrapParams.backupRequestParams, customerUUID);

      UUID newStorageConfigUUID = formData.bootstrapParams.backupRequestParams.storageConfigUUID;
      int newParallelism = formData.bootstrapParams.backupRequestParams.parallelism;
      if (!(drConfig.getStorageConfigUuid().equals(newStorageConfigUUID)
          && drConfig.getParallelism() == newParallelism)) {
        changeInParams = true;
      }
    }

    if (formData.pitrParams != null) {
      if (formData.pitrParams.snapshotIntervalSec == 0L) {
        formData.pitrParams.snapshotIntervalSec =
            Math.min(
                formData.pitrParams.retentionPeriodSec - 1, drConfig.getPitrSnapshotIntervalSec());
      }
      validatePitrParams(formData.pitrParams);
      Long oldRetentionPeriodSec = drConfig.getPitrRetentionPeriodSec();
      Long oldSnapshotIntervalSec = drConfig.getPitrSnapshotIntervalSec();

      if (!(oldRetentionPeriodSec != null
          && oldRetentionPeriodSec.equals(formData.pitrParams.retentionPeriodSec)
          && oldSnapshotIntervalSec != null
          && oldSnapshotIntervalSec.equals(formData.pitrParams.snapshotIntervalSec))) {
        changeInParams = true;
      }
    }

    if (formData.webhookUrls != null) {
      changeInParams = true;
      List<String> invalidUrls = new ArrayList<>();
      UrlValidator urlValidator = new UrlValidator(ALLOW_LOCAL_URLS);
      for (String webhookUrl : formData.webhookUrls) {
        if (!urlValidator.isValid(webhookUrl)) {
          invalidUrls.add(webhookUrl);
        }
      }
      if (!invalidUrls.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format("Invalid webhook urls were passed in. Invalid urls: %s", invalidUrls));
      }
    }

    if (!changeInParams) {
      throw new PlatformServiceException(BAD_REQUEST, "No changes were made to drConfig");
    }
  }

  public void validateBackupRequestParamsForBootstrapping(
      BootstrapBackupParams bootstrapBackupParams, UUID customerUUID) {
    XClusterConfigTaskBase.validateBackupRequestParamsForBootstrapping(
        customerConfigService, backupHelper, bootstrapBackupParams, customerUUID);
  }

  private void validatePitrParams(PitrParams pitrParams) {
    if (pitrParams.retentionPeriodSec < 5 * 60) {
      throw new PlatformServiceException(
          BAD_REQUEST, "pitr retentionPeriodSec must be greater than or equal to 5 minutes");
    }
    if (pitrParams.snapshotIntervalSec < 0) {
      throw new PlatformServiceException(
          BAD_REQUEST, "pitr snapshotIntervalSec must be greater than or equal to 0");
    }
    if (pitrParams.retentionPeriodSec <= pitrParams.snapshotIntervalSec) {
      throw new PlatformServiceException(
          BAD_REQUEST, "pitr retentionPeriodSec must be greater than snapshotIntervalSec");
    }
  }

  public BootstrapParams getBootstrapParamsFromRestartBootstrapParams(
      @Nullable RestartBootstrapParams restartBootstrapParams, Set<String> tableIds) {
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
  private void drSwitchoverFailoverPreChecks(
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

  private void drDBScopedSwitchoverPreChecks(
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
}
