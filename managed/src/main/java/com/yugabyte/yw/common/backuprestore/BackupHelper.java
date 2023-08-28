package com.yugabyte.yw.common.backuprestore;

import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static java.lang.Math.max;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.google.common.collect.Lists;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerLocationBackupInfo;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.DeleteBackupParams;
import com.yugabyte.yw.forms.DeleteBackupParams.DeleteBackupInfo;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.RestorePreflightParams;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.RelationType;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.NamespaceType;

@Slf4j
@Singleton
public class BackupHelper {
  private static final String VALID_OWNER_REGEX = "^[\\pL_][\\pL\\pM_0-9]*$";
  private static final int maxRetryCount = 5;

  private YbcManager ybcManager;
  private YBClientService ybClientService;
  private CustomerConfigService customerConfigService;
  private RuntimeConfGetter confGetter;
  private StorageUtilFactory storageUtilFactory;
  @Inject Commissioner commissioner;

  @Inject
  public BackupHelper(
      YbcManager ybcManager,
      YBClientService ybClientService,
      CustomerConfigService customerConfigService,
      RuntimeConfGetter confGetter,
      StorageUtilFactory storageUtilFactory,
      Commissioner commisssioner) {
    this.ybcManager = ybcManager;
    this.ybClientService = ybClientService;
    this.customerConfigService = customerConfigService;
    this.confGetter = confGetter;
    this.storageUtilFactory = storageUtilFactory;
    // this.commissioner = commissioner;
  }

  public String getValidOwnerRegex() {
    return VALID_OWNER_REGEX;
  }

  public boolean abortBackupTask(UUID taskUUID) {
    boolean status = commissioner.abortTask(taskUUID);
    return status;
  }

  public List<UUID> getBackupUUIDList(UUID taskUUID) {
    List<Backup> backups = Backup.fetchAllBackupsByTaskUUID(taskUUID);
    List<UUID> uuidList = new ArrayList<UUID>();

    for (Backup b : backups) {
      uuidList.add(b.getBackupUUID());
    }
    return uuidList;
  }

  public void scheduleBackupDeletionTasks(List<UUID> backupUUIDList) {}

  public void validateIncrementalScheduleFrequency(
      long frequency, long fullBackupFrequency, Universe universe) {
    long minimumIncrementalBackupScheduleFrequency =
        max(
            confGetter.getConfForScope(
                    universe, UniverseConfKeys.minIncrementalScheduleFrequencyInSecs)
                * 1000L,
            Util.YB_SCHEDULER_INTERVAL * 60 * 1000L);
    if (frequency < minimumIncrementalBackupScheduleFrequency) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Minimum incremental backup schedule duration is "
              + minimumIncrementalBackupScheduleFrequency
              + " milliseconds");
    }
    if (frequency >= fullBackupFrequency) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Incremental backup frequency should be lower than full backup frequency.");
    }
  }

  public UUID createBackupTask(UUID customerUUID, BackupRequestParams taskParams) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID(), customer);
    taskParams.customerUUID = customerUUID;

    if (universe
        .getConfig()
        .getOrDefault(Universe.TAKE_BACKUPS, "true")
        .equalsIgnoreCase("false")) {
      throw new PlatformServiceException(BAD_REQUEST, "Taking backups on the universe is disabled");
    }

    if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          CONFLICT,
          String.format(
              "Cannot run Backup task since the universe %s is currently in a locked state.",
              taskParams.getUniverseUUID().toString()));
    }
    if ((universe.getLiveTServersInPrimaryCluster().size() < taskParams.parallelDBBackups)
        || taskParams.parallelDBBackups <= 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "invalid parallel backups value provided for universe %s",
              universe.getUniverseUUID()));
    }
    validateBackupRequest(taskParams.keyspaceTableList, universe, taskParams.backupType);

    if (taskParams.timeBeforeDelete != 0L && taskParams.expiryTimeUnit == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Please provide time unit for backup expiry");
    }

    if (taskParams.storageConfigUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Missing StorageConfig UUID: " + taskParams.storageConfigUUID);
    }
    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);

    if (!customerConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot create backup as config is queued for deletion.");
    }
    if (taskParams.baseBackupUUID != null) {
      Backup previousBackup =
          Backup.getLastSuccessfulBackupInChain(customerUUID, taskParams.baseBackupUUID);
      if (!universe.isYbcEnabled()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Incremental backup not allowed for non-YBC universes");
      } else if (previousBackup == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "No previous successful backup found, please trigger a new base backup.");
      }
      validateStorageConfigOnBackup(customerConfig, previousBackup);
    } else {
      validateStorageConfig(customerConfig);
    }
    UUID taskUUID = commissioner.submit(TaskType.CreateBackup, taskParams);
    log.info("Submitted task to universe {}, task uuid = {}.", universe.getName(), taskUUID);
    CustomerTask.create(
        customer,
        taskParams.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Create,
        universe.getName());
    log.info("Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.getName());
    return taskUUID;
  }

  public UUID createRestoreTask(UUID customerUUID, RestoreBackupParams taskParams) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    taskParams.backupStorageInfoList.forEach(
        bSI -> {
          if (StringUtils.isNotBlank(bSI.newOwner)
              && !Pattern.matches(VALID_OWNER_REGEX, bSI.newOwner)) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Invalid owner rename during restore operation");
          }
        });

    taskParams.customerUUID = customerUUID;
    taskParams.prefixUUID = UUID.randomUUID();
    UUID universeUUID = taskParams.getUniverseUUID();
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (CollectionUtils.isEmpty(taskParams.backupStorageInfoList)) {
      throw new PlatformServiceException(BAD_REQUEST, "Backup information not provided");
    }
    validateRestoreOverwrites(taskParams.backupStorageInfoList, universe, taskParams.category);
    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (!customerConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot restore backup as config is queued for deletion.");
    }
    // Even though we check with default location below, this is needed to validate
    // regional locations, because their validity is not known to us when we send restore
    // request with a config.
    validateStorageConfig(customerConfig);
    CustomerConfigStorageData configData =
        (CustomerConfigStorageData) customerConfig.getDataObject();

    storageUtilFactory
        .getStorageUtil(customerConfig.getName())
        .validateStorageConfigOnLocationsList(
            configData,
            taskParams
                .backupStorageInfoList
                .parallelStream()
                .map(bSI -> bSI.storageLocation)
                .collect(Collectors.toSet()));

    if (taskParams.category.equals(BackupCategory.YB_CONTROLLER) && !universe.isYbcEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot restore the ybc backup as ybc is not installed on the universe");
    }
    UUID taskUUID = commissioner.submit(TaskType.RestoreBackup, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Restore,
        universe.getName());
    return taskUUID;
  }

  public List<YBPTask> createDeleteBackupTasks(
      UUID customerUUID, DeleteBackupParams deleteBackupParams) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    List<YBPTask> taskList = new ArrayList<>();
    for (DeleteBackupInfo deleteBackupInfo : deleteBackupParams.deleteBackupInfos) {
      UUID backupUUID = deleteBackupInfo.backupUUID;
      Backup backup = Backup.maybeGet(customerUUID, backupUUID).orElse(null);
      if (backup == null) {
        log.error("Can not delete {} backup as it is not present in the database.", backupUUID);
      } else {
        if (Backup.IN_PROGRESS_STATES.contains(backup.getState())) {
          log.error(
              "Backup {} is in the state {}. Deletion is not allowed",
              backupUUID,
              backup.getState());
        } else {
          UUID storageConfigUUID = deleteBackupInfo.storageConfigUUID;
          if (storageConfigUUID == null) {
            // Pick default backup storage config to delete the backup if not provided.
            storageConfigUUID = backup.getBackupInfo().storageConfigUUID;
          }
          if (backup.isIncrementalBackup() && backup.getState().equals(BackupState.Completed)) {
            // Currently, we don't allow users to delete successful standalone incremental backups.
            // They can only delete the full backup, along which all the incremental backups
            // will also be deleted.
            log.error(
                "Cannot delete backup {} as it in {} state",
                backup.getBackupUUID(),
                backup.getState());
            continue;
          }
          BackupTableParams params = backup.getBackupInfo();
          params.storageConfigUUID = storageConfigUUID;
          backup.updateBackupInfo(params);
          DeleteBackupYb.Params taskParams = new DeleteBackupYb.Params();
          taskParams.customerUUID = customerUUID;
          taskParams.backupUUID = backupUUID;
          taskParams.deleteForcefully = deleteBackupParams.deleteForcefully;
          UUID taskUUID = commissioner.submit(TaskType.DeleteBackupYb, taskParams);
          log.info("Saved task uuid {} in customer tasks for backup {}.", taskUUID, backupUUID);
          String target =
              !StringUtils.isEmpty(backup.getUniverseName())
                  ? backup.getUniverseName()
                  : String.format("univ-%s", backup.getUniverseUUID().toString());
          CustomerTask.create(
              customer,
              backup.getUniverseUUID(),
              taskUUID,
              CustomerTask.TargetType.Backup,
              CustomerTask.TaskType.Delete,
              target);
          taskList.add(new YBPTask(taskUUID, taskParams.backupUUID));
        }
      }
    }
    return taskList;
  }

  public boolean stopBackup(UUID customerUUID, UUID backupUUID) {
    Customer.getOrBadRequest(customerUUID);
    Process process = Util.getProcessOrBadRequest(backupUUID);
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    if (backup.getState() != Backup.BackupState.InProgress) {
      log.info("The backup {} you are trying to stop is not in progress.", backupUUID);
      throw new PlatformServiceException(
          BAD_REQUEST, "The backup you are trying to stop is not in progress.");
    }
    if (process == null) {
      log.info("The backup {} process you want to stop doesn't exist.", backupUUID);
      throw new PlatformServiceException(
          BAD_REQUEST, "The backup process you want to stop doesn't exist.");
    } else {
      process.destroyForcibly();
    }
    Util.removeProcess(backupUUID);
    try {
      waitForTask(backup.getTaskUUID());
    } catch (InterruptedException e) {
      log.info("Error while waiting for the backup task to get finished.");
    }
    backup.transitionState(BackupState.Stopped);
    return true;
  }

  public static void waitForTask(UUID taskUUID) throws InterruptedException {
    int numRetries = 0;
    while (numRetries < maxRetryCount) {
      TaskInfo taskInfo = TaskInfo.get(taskUUID);
      if (TaskInfo.COMPLETED_STATES.contains(taskInfo.getTaskState())) {
        return;
      }
      Thread.sleep(1000);
      numRetries++;
    }
    throw new PlatformServiceException(
        BAD_REQUEST,
        "WaitFor task exceeded maxRetries! Task state is " + TaskInfo.get(taskUUID).getTaskState());
  }

  public void validateStorageConfigOnBackup(Backup backup) {
    CustomerConfig config =
        customerConfigService.getOrBadRequest(
            backup.getCustomerUUID(), backup.getStorageConfigUUID());
    validateStorageConfigOnBackup(config, backup);
  }

  public void validateStorageConfigOnBackup(CustomerConfig config, Backup backup) {
    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(config.getName());
    List<BackupTableParams> backupParams = backup.getBackupParamsCollection();
    storageUtil.validateStorageConfigOnBackup(config.getDataObject(), backupParams);
  }

  public void validateStorageConfig(CustomerConfig config) throws PlatformServiceException {
    log.info(String.format("Validating storage config %s", config.getConfigName()));
    CustomerConfigStorageData configData = (CustomerConfigStorageData) config.getDataObject();
    if (StringUtils.isBlank(configData.backupLocation)) {
      throw new PlatformServiceException(BAD_REQUEST, "Default backup location cannot be empty");
    }
    storageUtilFactory.getStorageUtil(config.getName()).validateStorageConfig(configData);
  }

  // API facing restore overwrite check to fail upfront at controller level. This is
  // not a comprehensive check( atleast for YBC, for yb_backup this is the most we can do ).
  public void validateRestoreOverwrites(
      List<BackupStorageInfo> backupStorageInfos, Universe universe, Backup.BackupCategory category)
      throws PlatformServiceException {
    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    for (BackupStorageInfo backupInfo : backupStorageInfos) {
      if (!backupInfo.backupType.equals(TableType.REDIS_TABLE_TYPE)) {
        if (backupInfo.backupType.equals(TableType.YQL_TABLE_TYPE)
            && CollectionUtils.isNotEmpty(backupInfo.tableNameList)) {
          List<TableInfo> tableInfos =
              tableInfoList
                  .parallelStream()
                  .filter(tableInfo -> backupInfo.backupType.equals(tableInfo.getTableType()))
                  .filter(
                      tableInfo -> backupInfo.keyspace.equals(tableInfo.getNamespace().getName()))
                  .filter(tableInfo -> backupInfo.tableNameList.contains(tableInfo.getName()))
                  .collect(Collectors.toList());
          if (CollectionUtils.isNotEmpty(tableInfos)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Keyspace %s contains tables with same names, overwriting data is not allowed",
                    backupInfo.keyspace));
          }
        } else if (backupInfo.backupType.equals(TableType.PGSQL_TABLE_TYPE)) {
          List<TableInfo> tableInfos =
              tableInfoList
                  .parallelStream()
                  .filter(tableInfo -> backupInfo.backupType.equals(tableInfo.getTableType()))
                  .filter(
                      tableInfo -> backupInfo.keyspace.equals(tableInfo.getNamespace().getName()))
                  .collect(Collectors.toList());
          if (CollectionUtils.isNotEmpty(tableInfos)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Keyspace %s already exists, overwriting data is not allowed",
                    backupInfo.keyspace));
          }
        }
      }
    }
  }

  /**
   * Validate Objects to be restored wth actual universe content for YBC restore. Throw exception
   * if,
   *
   * <p>For YCQL: Table level overwrite is attempted.
   *
   * <p>For YSQL: Keyspace level overwrite is attempted.
   *
   * @param universeUUID UUID of the universe
   * @param restoreMap Map of TableType and corresponding Keyspace<->Tables if applicable. This map
   *     is used for checking overwrites, and is generated by BackupUtil method tagged below.
   * @see com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil#generateMapToRestoreNonRedisYBC
   */
  public void validateMapToRestoreWithUniverseNonRedisYBC(
      UUID universeUUID, Map<TableType, Map<String, Set<String>>> restoreMap) {
    List<TableInfo> tableInfos = getTableInfosOrEmpty(Universe.getOrBadRequest(universeUUID));
    tableInfos
        .parallelStream()
        .filter(t -> !t.getTableType().equals(TableType.REDIS_TABLE_TYPE))
        .forEach(
            t -> {
              if (restoreMap.containsKey(t.getTableType())
                  && restoreMap.get(t.getTableType()).containsKey(t.getNamespace().getName())) {
                if (t.getNamespace().getDatabaseType().equals(YQLDatabase.YQL_DATABASE_PGSQL)) {
                  throw new PlatformServiceException(
                      PRECONDITION_FAILED,
                      String.format(
                          "Restore attempting overwrite on YSQL keyspace %s.",
                          t.getNamespace().getName()));
                } else if (restoreMap
                    .get(t.getTableType())
                    .get(t.getNamespace().getName())
                    .contains(t.getName())) {
                  throw new PlatformServiceException(
                      PRECONDITION_FAILED,
                      String.format(
                          "Restore attempting overwrite for table %s on keyspace %s.",
                          t.getName(), t.getNamespace().getName()));
                }
              }
            });
  }

  public void validateStorageConfigForRestoreTask(
      UUID storageConfigUUID, UUID customerUUID, Collection<YbcBackupResponse> successMarkers) {
    CustomerConfig storageConfig =
        customerConfigService.getOrBadRequest(customerUUID, storageConfigUUID);
    CustomerConfigData configData = storageConfig.getDataObject();
    successMarkers.stream()
        .forEach(
            sM ->
                storageUtilFactory
                    .getStorageUtil(storageConfig.getName())
                    .validateStorageConfigOnSuccessMarker(configData, sM));
  }

  public void validateBackupRequest(
      List<KeyspaceTable> keyspaceTableList, Universe universe, TableType tableType) {
    if (CollectionUtils.isEmpty(keyspaceTableList)) {
      validateTables(null, universe, null, tableType);
    } else {
      // Verify tables to be backed up are not repeated across parts of request.
      Map<String, Set<UUID>> perKeyspaceTables = new HashMap<>();
      keyspaceTableList.stream()
          .forEach(
              kT -> {
                if (perKeyspaceTables.containsKey(kT.keyspace)) {
                  if (CollectionUtils.isEmpty(perKeyspaceTables.get(kT.keyspace))
                      || CollectionUtils.containsAny(
                          perKeyspaceTables.get(kT.keyspace), kT.tableUUIDList)
                      || CollectionUtils.isEmpty(kT.tableUUIDList)) {
                    throw new PlatformServiceException(
                        BAD_REQUEST,
                        String.format(
                            "Repeated tables in backup request for keyspace %s", kT.keyspace));
                  } else {
                    perKeyspaceTables.computeIfPresent(
                        kT.keyspace,
                        (keyspace, tableSet) -> {
                          tableSet.addAll(kT.tableUUIDList);
                          return tableSet;
                        });
                  }
                } else {
                  perKeyspaceTables.put(kT.keyspace, new HashSet<>(kT.tableUUIDList));
                }
              });
      perKeyspaceTables.entrySet().stream()
          .forEach(
              entry ->
                  validateTables(
                      Lists.newArrayList(entry.getValue()), universe, entry.getKey(), tableType));
    }
  }

  public void validateTables(
      List<UUID> tableUuids, Universe universe, String keyspace, TableType tableType)
      throws PlatformServiceException {

    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    if (keyspace != null && CollectionUtils.isEmpty(tableUuids)) {
      tableInfoList =
          tableInfoList
              .parallelStream()
              .filter(tableInfo -> keyspace.equals(tableInfo.getNamespace().getName()))
              .filter(tableInfo -> tableType.equals(tableInfo.getTableType()))
              .collect(Collectors.toList());
      if (CollectionUtils.isEmpty(tableInfoList)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot initiate backup with empty Keyspace " + keyspace);
      }
      return;
    }

    if (keyspace == null) {
      tableInfoList =
          tableInfoList
              .parallelStream()
              .filter(tableInfo -> tableType.equals(tableInfo.getTableType()))
              .collect(Collectors.toList());
      if (CollectionUtils.isEmpty(tableInfoList)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "No tables to backup inside specified Universe "
                + universe.getUniverseUUID().toString()
                + " and Table Type "
                + tableType.name());
      }
      return;
    }

    // Match if the table is an index or ysql table.
    if (CollectionUtils.isNotEmpty(tableUuids)) {
      for (TableInfo tableInfo : tableInfoList) {
        if (tableUuids.contains(
            getUUIDRepresentation(tableInfo.getId().toStringUtf8().replace("-", "")))) {
          if (tableInfo.hasRelationType()
              && tableInfo.getRelationType() == RelationType.INDEX_TABLE_RELATION) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Cannot backup index table " + tableInfo.getName());
          } else if (tableInfo.hasTableType()
              && tableInfo.getTableType() == TableType.PGSQL_TABLE_TYPE) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Cannot backup ysql table " + tableInfo.getName());
          }
        }
      }
    }
  }

  public List<TableInfo> getTableInfosOrEmpty(Universe universe) throws PlatformServiceException {
    final String masterAddresses = universe.getMasterAddresses(true);
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Masters are not currently queryable.");
    }
    YBClient client = null;
    try {
      String certificate = universe.getCertificateNodetoNode();
      client = ybClientService.getClient(masterAddresses, certificate);
      return client.getTablesList().getTableInfoList();
    } catch (Exception e) {
      log.warn(e.toString());
      return Collections.emptyList();
    } finally {
      ybClientService.closeClient(client, masterAddresses);
    }
  }

  /**
   * Generate preflight response for restore. Validates and provides output in form of
   * RestorePreflightResponse.
   *
   * @param preflightParams The RestorePreflightParams to validate
   * @param customerUUID The customerUUID identifier
   */
  public RestorePreflightResponse generateRestorePreflightAPIResponse(
      RestorePreflightParams preflightParams, UUID customerUUID) {

    // Validate storage config exists
    CustomerConfig storageConfig =
        customerConfigService.getOrBadRequest(customerUUID, preflightParams.getStorageConfigUUID());

    // Validate Universe exists
    Universe.getOrBadRequest(preflightParams.getUniverseUUID());

    // Validate storage config is usable
    storageUtilFactory
        .getStorageUtil(storageConfig.getName())
        .validateStorageConfigOnLocationsList(
            storageConfig.getDataObject(), preflightParams.getBackupLocations());
    UUID backupUUID = preflightParams.getBackupUUID();
    if (backupUUID != null) {
      Optional<Backup> oBackup = Backup.maybeGet(customerUUID, backupUUID);
      if (oBackup.isPresent()) {
        return restorePreflightWithBackupObject(customerUUID, oBackup.get(), preflightParams, true);
      }
    }
    return restorePreflightWithoutBackupObject(customerUUID, preflightParams, storageConfig, true);
  }

  /**
   * Method which runs restore preflight when backup_uuid is provided( and exists ) in Restore
   * preflight API.
   *
   * @param customerUUID
   * @param backup
   * @param preflightParams
   * @return RestorePreflightResponse
   */
  public RestorePreflightResponse restorePreflightWithBackupObject(
      UUID customerUUID,
      Backup backup,
      RestorePreflightParams preflightParams,
      boolean filterIndexes) {

    RestorePreflightResponse.RestorePreflightResponseBuilder preflightResponseBuilder =
        RestorePreflightResponse.builder();

    BackupCategory bCategory = backup.getCategory();
    preflightResponseBuilder.backupCategory(bCategory);
    if (bCategory.equals(BackupCategory.YB_CONTROLLER)
        && !Universe.getOrBadRequest(preflightParams.getUniverseUUID()).isYbcEnabled()) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED,
          "YB-Controller restore attempted on non YB-Controller enabled Universe");
    }

    // Whether backup has KMS history
    preflightResponseBuilder.hasKMSHistory(backup.isHasKMSHistory());

    // Whether selective restore would be supported for this Universe
    boolean selectiveRestoreYbcCheck = false;
    if (bCategory.equals(BackupCategory.YB_CONTROLLER)) {
      selectiveRestoreYbcCheck =
          ybcManager
              .getEnabledBackupFeatures(preflightParams.getUniverseUUID())
              .getSelectiveTableRestore();
    }

    // Generate locations and corresponding table list map.
    Map<String, PerLocationBackupInfo> locationContentMap =
        BackupUtil.getBackupLocationBackupInfoMap(
            backup.getBackupParamsCollection(), selectiveRestoreYbcCheck, filterIndexes);
    Map<String, PerLocationBackupInfo> locationBackupInfoMap =
        preflightParams.getBackupLocations().stream()
            .collect(
                Collectors.toMap(
                    Function.identity(),
                    bL -> {
                      if (!locationContentMap.containsKey(bL)) {
                        throw new RuntimeException(
                            String.format(
                                "Backup %s does not contain location %s",
                                backup.getBackupUUID().toString(), bL));
                      }
                      return locationContentMap.get(bL);
                    }));
    preflightResponseBuilder.perLocationBackupInfoMap(locationBackupInfoMap);
    return preflightResponseBuilder.build();
  }

  public RestorePreflightResponse restorePreflightWithoutBackupObject(
      UUID customerUUID, RestorePreflightParams preflightParams, boolean filterIndexes) {
    return restorePreflightWithoutBackupObject(
        customerUUID,
        preflightParams,
        customerConfigService.getOrBadRequest(customerUUID, preflightParams.getStorageConfigUUID()),
        filterIndexes);
  }

  /**
   * Method which runs restore preflight without backup object( advanced restore ). This is not
   * light weight and will perform metadata download to carry out preflight validations.
   *
   * @param customerUUID
   * @param preflightParams
   */
  public RestorePreflightResponse restorePreflightWithoutBackupObject(
      UUID customerUUID,
      RestorePreflightParams preflightParams,
      CustomerConfig storageConfig,
      boolean filterIndexes) {
    RestorePreflightResponse preflightResponse = null;

    // Determine if success file exists on all backup Locations.
    boolean ybcSuccessMarkerExists =
        checkFileExistsOnBackupLocation(
            storageConfig,
            preflightParams.getBackupLocations(),
            preflightParams.getUniverseUUID(),
            YbcBackupUtil.YBC_SUCCESS_MARKER_FILE_NAME,
            true);

    // If success file exists
    if (ybcSuccessMarkerExists) {
      if (!Universe.getOrBadRequest(preflightParams.getUniverseUUID()).isYbcEnabled()) {
        throw new PlatformServiceException(
            PRECONDITION_FAILED,
            "YB-Controller restore attempted on non YB-Controller enabled Universe");
      }
      preflightResponse =
          generateYBCRestorePreflightResponseWithoutBackupObject(
              preflightParams, storageConfig, filterIndexes);
    } else {
      log.info("Did not find YB-Controller success marker, fallback to script");
      preflightResponse =
          storageUtilFactory
              .getStorageUtil(storageConfig.getName())
              .generateYBBackupRestorePreflightResponseWithoutBackupObject(
                  preflightParams, storageConfig.getDataObject());
    }
    return preflightResponse;
  }

  /**
   * Generate RestorePreflightResponse for YBC backup locations.
   *
   * @param preflightParams The RestorePreflightParams object
   * @param storageConfig The CustomerConfig object
   */
  public RestorePreflightResponse generateYBCRestorePreflightResponseWithoutBackupObject(
      RestorePreflightParams preflightParams, CustomerConfig storageConfig, boolean filterIndexes) {
    Map<String, YbcBackupResponse> ybcSuccessMarkerMap =
        getYbcSuccessMarker(
            storageConfig, preflightParams.getBackupLocations(), preflightParams.getUniverseUUID());

    boolean selectiveRestoreYbcCheck =
        ybcManager
            .getEnabledBackupFeatures(preflightParams.getUniverseUUID())
            .getSelectiveTableRestore();

    return YbcBackupUtil.generateYBCRestorePreflightResponseUsingMetadata(
        ybcSuccessMarkerMap, selectiveRestoreYbcCheck, filterIndexes);
  }

  // Pass list of locations and particular file. The checkExistsOnAll
  // will determine OR or AND of file existence.
  public boolean checkFileExistsOnBackupLocation(
      CustomerConfig storageConfig,
      Set<String> storagelocationList,
      UUID universeUUID,
      String fileName,
      boolean checkExistsOnAll) {
    try {
      return storageUtilFactory
          .getStorageUtil(storageConfig.getName())
          .checkFileExists(
              storageConfig.getDataObject(),
              storagelocationList,
              fileName,
              universeUUID,
              checkExistsOnAll);
    } catch (Exception e) {
      throw new RuntimeException(
          "Checking file existence on storage location failed", e.getCause());
    }
  }

  /**
   * Get YBC success marker from backup locations, and parse them to YbcBackupResponse class.
   * Expects YBC to be up and running, since YBC restores possible if that is true.
   *
   * @param storageConfig The storage config to use
   * @param storageLocationList List of locations to get the success marker file from
   * @param universeUUID The universe_uuid of target Universe
   */
  public Map<String, YbcBackupResponse> getYbcSuccessMarker(
      CustomerConfig storageConfig, Set<String> storageLocationList, UUID universeUUID) {
    Map<String, String> successMarkerStrs = new HashMap<>();
    try {
      storageLocationList.stream()
          .forEach(
              bL -> {
                CloudStoreSpec successMarkerCSSpec =
                    storageUtilFactory
                        .getStorageUtil(storageConfig.getName())
                        .createDsmCloudStoreSpec(bL, storageConfig.getDataObject());
                String taskId = UUID.randomUUID().toString() + "_preflight_success_marker";
                // Need to assign some namespace type here, real context does not change.
                BackupServiceTaskCreateRequest smDownloadRequest =
                    YbcBackupUtil.createDsmRequest(successMarkerCSSpec, taskId, NamespaceType.YCQL);
                String successMarkerStr =
                    ybcManager.downloadSuccessMarker(smDownloadRequest, universeUUID, taskId);
                if (StringUtils.isBlank(successMarkerStr)) {
                  throw new RuntimeException(
                      String.format(
                          "Unable to download success marker for backup location %s", bL));
                }
                successMarkerStrs.put(bL, successMarkerStr);
              });
    } catch (Exception e) {
      throw new RuntimeException("Fetching success marker failed", e.getCause());
    }
    try {
      return successMarkerStrs.entrySet().stream()
          .collect(
              Collectors.toMap(
                  Map.Entry::getKey, e -> YbcBackupUtil.parseYbcBackupResponse(e.getValue())));
    } catch (Exception e) {
      throw new RuntimeException("Parse error for success marker files", e.getCause());
    }
  }
}
