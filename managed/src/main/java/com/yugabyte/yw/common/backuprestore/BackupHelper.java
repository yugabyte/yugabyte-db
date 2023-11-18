package com.yugabyte.yw.common.backuprestore;

import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static java.lang.Math.max;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.Lists;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceQueryResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerLocationBackupInfo;
import com.yugabyte.yw.common.backuprestore.BackupUtil.TablespaceResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.logging.LogUtil;
import com.yugabyte.yw.common.replication.ValidateReplicationInfo;
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
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.backuprestore.Tablespace;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
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
import org.apache.commons.collections4.Predicate;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.MDC;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.RelationType;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.CloudStoreConfig;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.NamespaceType;
import play.libs.Json;

@Slf4j
@Singleton
public class BackupHelper {
  private static final String VALID_OWNER_REGEX = "^[\\pL_][\\pL\\pM_0-9]*$";
  private static final int maxRetryCount = 5;
  private static final String TABLESPACES_SQL_QUERY =
      "SELECT jsonb_agg(t) from (SELECT spcname, spcoptions"
          + " from pg_catalog.pg_tablespace where spcname not like 'pg_%') as t;";

  private YbcManager ybcManager;
  private YBClientService ybClientService;
  private CustomerConfigService customerConfigService;
  private RuntimeConfGetter confGetter;
  private StorageUtilFactory storageUtilFactory;
  private ValidateReplicationInfo validateReplicationInfo;
  private NodeUniverseManager nodeUniverseManager;
  private YbcBackupUtil ybcBackupUtil;
  @Inject Commissioner commissioner;

  @Inject
  public BackupHelper(
      YbcManager ybcManager,
      YBClientService ybClientService,
      CustomerConfigService customerConfigService,
      RuntimeConfGetter confGetter,
      StorageUtilFactory storageUtilFactory,
      Commissioner commisssioner,
      ValidateReplicationInfo validateReplicationInfo,
      NodeUniverseManager nodeUniverseManager,
      YbcBackupUtil ybcBackupUtil) {
    this.ybcManager = ybcManager;
    this.ybClientService = ybClientService;
    this.customerConfigService = customerConfigService;
    this.confGetter = confGetter;
    this.storageUtilFactory = storageUtilFactory;
    this.validateReplicationInfo = validateReplicationInfo;
    this.nodeUniverseManager = nodeUniverseManager;
    this.ybcBackupUtil = ybcBackupUtil;
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
    UniverseDefinitionTaskParams.UserIntent primaryClusterUserIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
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
    if (taskParams.parallelDBBackups <= 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Invalid parallel backups value provided for universe %s",
              universe.getUniverseUUID()));
    }

    if (taskParams.timeBeforeDelete != 0L && taskParams.expiryTimeUnit == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Please provide time unit for backup expiry");
    }

    if (taskParams.backupType != null) {
      if (taskParams.backupType.equals(TableType.PGSQL_TABLE_TYPE)
          && !primaryClusterUserIntent.enableYSQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YSQL tables if API is disabled");
      } else if (taskParams.backupType.equals(TableType.YQL_TABLE_TYPE)
          && !primaryClusterUserIntent.enableYCQL) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot take backups on YCQL tables if API is disabled");
      }
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
    }
    if (!isSkipConfigBasedPreflightValidation(universe)) {
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
    UUID universeUUID = taskParams.getUniverseUUID();
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    UniverseDefinitionTaskParams.UserIntent primaryClusterUserIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    taskParams.backupStorageInfoList.forEach(
        bSI -> {
          if (StringUtils.isNotBlank(bSI.newOwner)
              && !Pattern.matches(VALID_OWNER_REGEX, bSI.newOwner)) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Invalid owner rename during restore operation");
          }
          if (bSI.backupType != null) {
            if (bSI.backupType.equals(TableType.PGSQL_TABLE_TYPE)
                && !primaryClusterUserIntent.enableYSQL) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Cannot take backups on YSQL tables if API is disabled");
            } else if (bSI.backupType.equals(TableType.YQL_TABLE_TYPE)
                && !primaryClusterUserIntent.enableYCQL) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Cannot take backups on YCQL tables if API is disabled");
            }
          }
        });

    taskParams.customerUUID = customerUUID;
    taskParams.prefixUUID = UUID.randomUUID();

    if (CollectionUtils.isEmpty(taskParams.backupStorageInfoList)) {
      throw new PlatformServiceException(BAD_REQUEST, "Backup information not provided");
    }

    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(customerUUID, taskParams.storageConfigUUID);
    if (!customerConfig.getState().equals(ConfigState.Active)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot restore backup as config is queued for deletion.");
    }

    CustomerConfigStorageData configData =
        (CustomerConfigStorageData) customerConfig.getDataObject();

    if (!isSkipConfigBasedPreflightValidation(universe)) {
      storageUtilFactory
          .getStorageUtil(customerConfig.getName())
          .validateStorageConfigOnDefaultLocationsList(
              configData,
              taskParams.backupStorageInfoList.parallelStream()
                  .map(bSI -> bSI.storageLocation)
                  .collect(Collectors.toSet()));
    }

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

  public void validateRestoreOverwrites(
      List<BackupStorageInfo> backupStorageInfos, Universe universe)
      throws PlatformServiceException {
    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    for (BackupStorageInfo backupInfo : backupStorageInfos) {
      if (!backupInfo.backupType.equals(TableType.REDIS_TABLE_TYPE)) {
        if (backupInfo.backupType.equals(TableType.YQL_TABLE_TYPE)
            && CollectionUtils.isNotEmpty(backupInfo.tableNameList)) {
          List<TableInfo> tableInfos =
              tableInfoList.parallelStream()
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
              tableInfoList.parallelStream()
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
    tableInfos.parallelStream()
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

  /**
   * Validate YB-Controller based restores using success marker metadata. The method validates the
   * following 3 things:
   *
   * <p>1. The Storage config which is used for restore must contain all regions used in the backup.
   *
   * <p>2. The relative path of the backup i.e. the cloud directories inside the buckets must be
   * listable using the credentials provided.
   *
   * <p>3. The step (2) but done on database nodes(only default cloudDir) instead of YBA.
   *
   * @param storageConfigUUID
   * @param customerUUID
   * @param universeUUID
   * @param successMarkers
   */
  public void validateStorageConfigForYbcRestoreTask(
      UUID storageConfigUUID,
      UUID customerUUID,
      UUID universeUUID,
      Collection<YbcBackupResponse> successMarkers) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (isSkipConfigBasedPreflightValidation(universe)) {
      return;
    }
    CustomerConfig storageConfig =
        customerConfigService.getOrBadRequest(customerUUID, storageConfigUUID);
    CustomerConfigData configData = storageConfig.getDataObject();
    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(storageConfig.getName());
    Map<String, String> configLocationMap = storageUtil.getRegionLocationsMap(configData);
    for (YbcBackupResponse successMarker : successMarkers) {
      Map<String, YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation>
          successMarkerBucketLocationMap =
              successMarker.responseCloudStoreSpec.getBucketLocationsMap();
      successMarkerBucketLocationMap.entrySet().stream()
          .forEach(
              sME -> {
                if (!configLocationMap.containsKey(sME.getKey())) {
                  throw new PlatformServiceException(
                      PRECONDITION_FAILED,
                      String.format("Storage config does not contain region %s", sME.getKey()));
                }
              });
      // Verify all regions locations including default from Platform.
      storageUtil.checkListObjectsWithYbcSuccessMarkerCloudStore(
          configData, successMarker.responseCloudStoreSpec);
      // Verify on Universe nodes.
      validateStorageConfigForRestoreOnUniverse(
          storageConfig, universe, successMarker.responseCloudStoreSpec.getBucketLocationsMap());
    }
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
          tableInfoList.parallelStream()
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
          tableInfoList.parallelStream()
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
    final String masterAddresses = universe.getMasterAddresses();
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

  public List<Tablespace> getTablespacesInUniverse(Universe universe) {
    log.info("Fetching tablespaces for universe {}", universe.getName());
    final String masterAddresses = universe.getMasterAddresses(true);
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Masters are not currently queryable.");
    }
    NodeDetails nodeToUse = null;
    try {
      nodeToUse = CommonUtils.getServerToRunYsqlQuery(universe);
    } catch (IllegalStateException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cluster may not have been initialized yet. Please try later");
    }
    ShellResponse shellResponse =
        nodeUniverseManager.runYsqlCommand(nodeToUse, universe, "template1", TABLESPACES_SQL_QUERY);
    if (!shellResponse.isSuccess()) {
      log.warn(
          "Attempt to fetch tablespace info via node {} failed, response {}:{}",
          nodeToUse.nodeName,
          shellResponse.code,
          shellResponse.message);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching TableSpace information");
    }
    String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);
    if (jsonData == null || jsonData.isBlank()) {
      return new ArrayList<>();
    }
    try {
      ObjectMapper objectMapper = Json.mapper();
      List<TableSpaceQueryResponse> tablespaceList =
          objectMapper.readValue(jsonData, new TypeReference<List<TableSpaceQueryResponse>>() {});
      return tablespaceList.stream()
          .map(Tablespace::getTablespaceFromTablespaceQueryResponse)
          .collect(Collectors.toList());
    } catch (IOException e) {
      log.error("Unable to parse fetchTablespaceQuery response {}", jsonData, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching TableSpace information");
    }
  }

  public TablespaceResponse getTablespaceResponse(
      Universe universe,
      List<Tablespace> tablespacesInBackup,
      List<Tablespace> tablespacesInUniverse) {
    if (CollectionUtils.isEmpty(tablespacesInBackup)) {
      return TablespaceResponse.builder().containsTablespaces(false).build();
    }
    Map<String, Tablespace> tablespacesInBackupMap =
        tablespacesInBackup.parallelStream()
            .collect(Collectors.toMap(t -> t.tablespaceName, Function.identity()));

    // Conflicting tablespaces info.
    List<String> conflictingTablespaceNames = new ArrayList<>();
    if (CollectionUtils.isNotEmpty(tablespacesInUniverse)) {
      List<Tablespace> conflictingTablespaces =
          tablespacesInUniverse.stream()
              .filter(t -> tablespacesInBackupMap.containsKey(t.tablespaceName))
              .collect(Collectors.toList());
      conflictingTablespaces.stream()
          .forEach(
              t -> {
                Tablespace backupTs = tablespacesInBackupMap.get(t.tablespaceName);
                String placementComparison =
                    t.replicaPlacement.equals(backupTs.replicaPlacement)
                        ? "identical"
                        : "different";
                log.info(
                    "Tablespace {} with replica placement {}"
                        + " already exists in Universe {} with {}"
                        + " placement",
                    t.tablespaceName,
                    backupTs.getJsonString(),
                    universe.getName(),
                    placementComparison);
              });
      conflictingTablespaces.stream()
          .map(t -> t.tablespaceName)
          .forEach(tName -> conflictingTablespaceNames.add(tName));
      // Remove conflicting tablespaces, we don't need to validate them.
      CollectionUtils.filter(
          tablespacesInBackup,
          new Predicate<Tablespace>() {
            @Override
            public boolean evaluate(Tablespace tablespace) {
              return !conflictingTablespaceNames.contains(tablespace.tablespaceName);
            }
          });
    }
    List<String> unsupportedTablespaceNames =
        validateReplicationInfo
            .getUnsupportedTablespacesOnUniverse(universe, tablespacesInBackup)
            .parallelStream()
            .map(t -> t.tablespaceName)
            .collect(Collectors.toList());

    return TablespaceResponse.builder()
        .conflictingTablespaces(conflictingTablespaceNames)
        .unsupportedTablespaces(unsupportedTablespaceNames)
        .containsTablespaces(true)
        .build();
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
    // Get loggingID
    String loggingID = (String) MDC.get(LogUtil.CORRELATION_ID);
    // Validate Universe exists
    Universe universe = Universe.getOrBadRequest(preflightParams.getUniverseUUID());

    log.info(
        "Starting preflight checks for restore on Universe {} with regex filter {}",
        universe.getName(),
        loggingID);

    // Validate storage config exists
    CustomerConfig storageConfig =
        customerConfigService.getOrBadRequest(customerUUID, preflightParams.getStorageConfigUUID());

    // Validate storage config is usable
    storageUtilFactory
        .getStorageUtil(storageConfig.getName())
        .validateStorageConfigOnDefaultLocationsList(
            storageConfig.getDataObject(), preflightParams.getBackupLocations());

    UUID backupUUID = preflightParams.getBackupUUID();
    if (backupUUID != null) {
      Optional<Backup> oBackup = Backup.maybeGet(customerUUID, backupUUID);
      if (oBackup.isPresent()) {
        return restorePreflightWithBackupObject(customerUUID, oBackup.get(), preflightParams, true)
            .toBuilder()
            .loggingID(loggingID)
            .build();
      }
    }
    return restorePreflightWithoutBackupObject(customerUUID, preflightParams, storageConfig, true)
        .toBuilder()
        .loggingID(loggingID)
        .build();
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

    Universe universe = Universe.getOrBadRequest(preflightParams.getUniverseUUID());
    if (bCategory.equals(BackupCategory.YB_CONTROLLER) && !universe.isYbcEnabled()) {
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

    boolean queryUniverseTablespaces =
        backup.getBackupParamsCollection().parallelStream()
            .filter(bP -> CollectionUtils.isNotEmpty(bP.getTablespacesList()))
            .findAny()
            .isPresent();
    List<Tablespace> universeTablespaces =
        queryUniverseTablespaces ? getTablespacesInUniverse(universe) : new ArrayList<>();
    Map<String, TablespaceResponse> tablespaceResponsesMap =
        backup.getBackupParamsCollection().stream()
            .collect(
                Collectors.toMap(
                    bP -> bP.storageLocation,
                    bP ->
                        getTablespaceResponse(
                            universe, bP.getTablespacesList(), universeTablespaces)));

    // Generate locations and corresponding PerLocationBackupInfo map.
    Map<String, PerLocationBackupInfo> locationContentMap =
        BackupUtil.getBackupLocationBackupInfoMap(
            backup.getBackupParamsCollection(),
            selectiveRestoreYbcCheck,
            filterIndexes,
            tablespaceResponsesMap);
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
      Universe universe = Universe.getOrBadRequest(preflightParams.getUniverseUUID());
      if (!universe.isYbcEnabled()) {
        throw new PlatformServiceException(
            PRECONDITION_FAILED,
            "YB-Controller restore attempted on non YB-Controller enabled Universe");
      }
      preflightResponse =
          generateYBCRestorePreflightResponseWithoutBackupObject(
              preflightParams, storageConfig, filterIndexes, universe);
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
      RestorePreflightParams preflightParams,
      CustomerConfig storageConfig,
      boolean filterIndexes,
      Universe universe) {
    Map<String, YbcBackupResponse> ybcSuccessMarkerMap =
        getYbcSuccessMarker(
            storageConfig, preflightParams.getBackupLocations(), preflightParams.getUniverseUUID());

    boolean selectiveRestoreYbcCheck =
        ybcManager
            .getEnabledBackupFeatures(preflightParams.getUniverseUUID())
            .getSelectiveTableRestore();

    boolean queryUniverseTablespaces =
        ybcSuccessMarkerMap.values().parallelStream()
            .filter(yBP -> CollectionUtils.isNotEmpty(yBP.tablespaceInfos))
            .findAny()
            .isPresent();
    List<Tablespace> universeTablespaces =
        queryUniverseTablespaces ? getTablespacesInUniverse(universe) : new ArrayList<>();
    Map<String, TablespaceResponse> tablespaceResponsesMap =
        ybcSuccessMarkerMap.entrySet().stream()
            .collect(
                Collectors.toMap(
                    Map.Entry::getKey,
                    e ->
                        getTablespaceResponse(
                            universe, e.getValue().tablespaceInfos, universeTablespaces)));
    return YbcBackupUtil.generateYBCRestorePreflightResponseUsingMetadata(
        ybcSuccessMarkerMap, selectiveRestoreYbcCheck, filterIndexes, tablespaceResponsesMap);
  }

  public boolean checkFileExistsOnBackupLocation(
      UUID configUUID,
      UUID customerUUID,
      Set<String> storagelocationList,
      UUID universeUUID,
      String fileName,
      boolean checkExistsOnAll) {
    CustomerConfig storageConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    return checkFileExistsOnBackupLocation(
        storageConfig, storagelocationList, universeUUID, fileName, checkExistsOnAll);
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

  /**
   * Validate storage config for backup on Universe using YBC gRPC. Currently we only validate
   * default location. Adding region specific validation is a TODO.
   *
   * @param config
   * @param universe
   */
  public void validateStorageConfigForBackupOnUniverse(CustomerConfig config, Universe universe) {
    if (isSkipConfigBasedPreflightValidation(universe)) {
      return;
    }
    List<NodeDetails> nodeDetailsList = universe.getRunningTserversInPrimaryCluster();
    for (NodeDetails node : nodeDetailsList) {
      ybcManager.validateCloudConfigIgnoreIfYbcUnavailable(
          node.cloudInfo.private_ip,
          universe,
          ybcBackupUtil.getCloudStoreConfigWithProvidedRegions(config, null));
    }
  }

  /**
   * Validate storage config for Restore on Universe using YBC gRPC. We validate default region
   * configuration on available YBC servers. Adding region specific validation is a TODO.
   *
   * @param config
   * @param universe
   * @param bucketLocationsMap
   */
  public void validateStorageConfigForRestoreOnUniverse(
      CustomerConfig config,
      Universe universe,
      Map<String, YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation> bucketLocationsMap) {
    if (isSkipConfigBasedPreflightValidation(universe)) {
      return;
    }
    List<NodeDetails> nodeDetailsList = universe.getRunningTserversInPrimaryCluster();
    for (NodeDetails node : nodeDetailsList) {
      ybcManager.validateCloudConfigIgnoreIfYbcUnavailable(
          node.cloudInfo.private_ip,
          universe,
          ybcBackupUtil.getCloudStoreConfigWithBucketLocationsMap(config, bucketLocationsMap));
    }
  }

  public void validateStorageConfigForSuccessMarkerDownloadOnUniverse(
      CustomerConfig config, Universe universe, Set<String> successMarkerLocations) {
    if (isSkipConfigBasedPreflightValidation(universe)) {
      return;
    }
    List<NodeDetails> nodeDetailsList = universe.getRunningTserversInPrimaryCluster();
    for (String successMarkerLoc : successMarkerLocations) {
      CloudStoreConfig csConfig =
          CloudStoreConfig.newBuilder()
              .setDefaultSpec(
                  storageUtilFactory
                      .getStorageUtil(config.getName())
                      .createDsmCloudStoreSpec(successMarkerLoc, config.getDataObject()))
              .build();
      for (NodeDetails node : nodeDetailsList) {
        ybcManager.validateCloudConfigIgnoreIfYbcUnavailable(
            node.cloudInfo.private_ip, universe, csConfig);
      }
    }
  }

  /**
   * Validate storage config on running Universe nodes.
   *
   * @param customerConfig
   * @param universe
   * @param ybcBackup
   */
  public void validateStorageConfigOnRunningUniverseNodes(
      CustomerConfig customerConfig, Universe universe, boolean ybcBackup) {
    if (isSkipConfigBasedPreflightValidation(universe)) {
      return;
    }
    // Validate storage config on running Universe nodes.
    if (ybcBackup) {
      validateStorageConfigForBackupOnUniverse(customerConfig, universe);
    } else {
      // Only for NFS non-YBC universes.
      storageUtilFactory
          .getStorageUtil(customerConfig.getName())
          .validateStorageConfigOnUniverseNonRpc(customerConfig, universe);
    }
  }

  public boolean isSkipConfigBasedPreflightValidation(Universe universe) {
    return confGetter.getConfForScope(
        universe, UniverseConfKeys.skipConfigBasedPreflightValidation);
  }
}
