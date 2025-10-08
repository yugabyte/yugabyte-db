// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.dr;

import static com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase.getRequestedTableInfoList;
import static org.apache.commons.validator.routines.UrlValidator.ALLOW_LOCAL_URLS;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.XClusterCreatePrecheck;
import com.yugabyte.yw.common.XClusterUtil;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.XClusterConfigController;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.DrConfigEditForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.validator.routines.UrlValidator;
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
      CustomerConfigService customerConfigService) {
    this.confGetter = confGetter;
    this.commissioner = commissioner;
    this.softwareUpgradeHelper = softwareUpgradeHelper;
    this.xClusterCreatePrecheck = xClusterCreatePrecheck;
    this.autoFlagUtil = autoFlagUtil;
    this.ybService = ybService;
    this.backupHelper = backupHelper;
    this.customerConfigService = customerConfigService;
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
}
