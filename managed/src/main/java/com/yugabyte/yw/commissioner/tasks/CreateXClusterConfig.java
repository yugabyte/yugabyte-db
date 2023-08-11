// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.io.File;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;

@Slf4j
public class CreateXClusterConfig extends XClusterConfigTaskBase {

  public static final long TIME_BEFORE_DELETE_BACKUP_MS = TimeUnit.DAYS.toMillis(1);

  @Inject
  protected CreateXClusterConfig(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public List<Restore> restoreList = new ArrayList<>();

  public List<Backup> backupList = new ArrayList<>();

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    try {
      // Lock the source universe.
      lockUniverseForUpdate(sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion());
      try {
        // Lock the target universe.
        lockUniverseForUpdate(targetUniverse.getUniverseUUID(), targetUniverse.getVersion());

        createCheckXUniverseAutoFlag(sourceUniverse, targetUniverse)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.PreflightChecks);

        createXClusterConfigSetStatusTask(XClusterConfigStatusType.Updating);

        createXClusterConfigSetStatusForTablesTask(
            getTableIds(taskParams().getTableInfoList()), XClusterTableConfig.Status.Updating);

        addSubtasksToCreateXClusterConfig(
            sourceUniverse,
            targetUniverse,
            taskParams().getTableInfoList(),
            taskParams().getMainTableIndexTablesMap());

        createXClusterConfigSetStatusTask(XClusterConfigStatusType.Running)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(targetUniverse.getUniverseUUID())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.getUniverseUUID())
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

        getRunnableTask().runSubTasks();
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      // Set xCluster config status to failed.
      setXClusterConfigStatus(XClusterConfigStatusType.Failed);
      // Set tables in updating status to failed.
      Set<String> tablesInPendingStatus =
          xClusterConfig.getTableIdsInStatus(
              getTableIds(taskParams().getTableInfoList()),
              X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST);
      xClusterConfig.updateStatusForTables(
          tablesInPendingStatus, XClusterTableConfig.Status.Failed);
      // Set backup and restore status to failed and alter load balanced.
      boolean isLoadBalancerAltered = false;
      for (Restore restore : restoreList) {
        isLoadBalancerAltered = isLoadBalancerAltered || restore.isAlterLoadBalancer();
      }
      handleFailedBackupAndRestore(
          backupList, restoreList, false /* isAbort */, isLoadBalancerAltered);
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
    }

    log.info("Completed {}", getName());
  }

  protected void addSubtasksToCreateXClusterConfig(
      Universe sourceUniverse,
      Universe targetUniverse,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      Map<String, List<String>> mainTableIndexTablesMap) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    // Create namespaces for universe's clusters if both universes are k8s universes and MCS is
    // enabled.
    if (KubernetesUtil.isMCSEnabled(sourceUniverse)
        && KubernetesUtil.isMCSEnabled(targetUniverse)) {
      createReplicateNamespacesTask();
    }

    // Support mismatched TLS root certificates.
    Optional<File> sourceCertificate =
        getSourceCertificateIfNecessary(sourceUniverse, targetUniverse);
    sourceCertificate.ifPresent(
        cert ->
            createTransferXClusterCertsCopyTasks(
                xClusterConfig,
                targetUniverse.getNodes(),
                xClusterConfig.getReplicationGroupName(),
                cert,
                targetUniverse.getUniverseDetails().getSourceRootCertDirPath()));

    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        dbToTablesInfoMapNeedBootstrap =
            getDbToTablesInfoMapNeedBootstrap(
                getTableIds(requestedTableInfoList),
                requestedTableInfoList,
                mainTableIndexTablesMap,
                taskParams().getSourceTableIdsWithNoTableOnTargetUniverse());

    // Replication for tables that do NOT need bootstrapping.
    Set<String> tableIdsNotNeedBootstrap =
        getTableIdsNotNeedBootstrap(getTableIds(requestedTableInfoList));
    CommonTypes.TableType tableType = requestedTableInfoList.get(0).getTableType();
    if (!tableIdsNotNeedBootstrap.isEmpty()) {
      log.info(
          "Creating a subtask to set up replication without bootstrap for tables {}",
          tableIdsNotNeedBootstrap);

      // Set up PITRs for txn xCluster.
      if (xClusterConfig.getType().equals(ConfigType.Txn)) {
        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
            requestedTableInfoListNotNeedBootstrap =
                requestedTableInfoList.stream()
                    .filter(tableInfo -> tableIdsNotNeedBootstrap.contains(getTableId(tableInfo)))
                    .collect(Collectors.toList());
        Set<MasterTypes.NamespaceIdentifierPB> namespaces =
            getNamespaces(requestedTableInfoListNotNeedBootstrap);
        namespaces.forEach(
            namespace -> {
              Optional<PitrConfig> pitrConfigOptional =
                  PitrConfig.maybeGet(
                      xClusterConfig.getTargetUniverseUUID(), tableType, namespace.getName());

              if (xClusterConfig.isUsedForDr()) {
                // For DR, read the parameters from taskParams.
                if (Objects.isNull(taskParams().getPitrParams())) {
                  throw new IllegalArgumentException(
                      "pitrParams in taskParams cannot be null while creating an "
                          + "xCluster config for DR");
                }
                if (pitrConfigOptional.isPresent()) {
                  // Only delete and recreate if the PITR config parameters differ from taskParams.
                  if (pitrConfigOptional.get().getRetentionPeriod()
                          != taskParams().getPitrParams().retentionPeriodSec
                      || pitrConfigOptional.get().getScheduleInterval()
                          != taskParams().getPitrParams().snapshotIntervalSec) {
                    createDeletePitrConfigTask(pitrConfigOptional.get().getUuid());

                    createCreatePitrConfigTask(
                        namespace.getName(),
                        tableType,
                        taskParams().getPitrParams().retentionPeriodSec,
                        xClusterConfig);
                  } else {
                    xClusterConfig.addPitrConfig(pitrConfigOptional.get());
                  }
                } else {
                  createCreatePitrConfigTask(
                      namespace.getName(),
                      tableType,
                      taskParams().getPitrParams().retentionPeriodSec,
                      xClusterConfig);
                }
              } else {
                if (pitrConfigOptional.isPresent()) {
                  xClusterConfig.addPitrConfig(pitrConfigOptional.get());
                } else {
                  // Create a PITR config using default parameters.
                  createCreatePitrConfigTask(
                      namespace.getName(),
                      tableType,
                      confGetter
                          .getConfForScope(
                              targetUniverse,
                              UniverseConfKeys.txnXClusterPitrDefaultRetentionPeriod)
                          .getSeconds(),
                      xClusterConfig);
                }
              }
            });
      }

      // Set up the replication config.
      createXClusterConfigSetupTask(tableIdsNotNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
    }

    // Add the subtasks to set up replication for tables that need bootstrapping.
    addSubtasksForTablesNeedBootstrap(
        sourceUniverse,
        targetUniverse,
        taskParams().getBootstrapParams(),
        dbToTablesInfoMapNeedBootstrap,
        !tableIdsNotNeedBootstrap.isEmpty());
  }

  protected void addSubtasksForTablesNeedBootstrap(
      Universe sourceUniverse,
      Universe targetUniverse,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
          dbToTablesInfoMapNeedBootstrap,
      boolean isReplicationConfigCreated) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    for (String namespaceName : dbToTablesInfoMapNeedBootstrap.keySet()) {
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoListNeedBootstrap =
          dbToTablesInfoMapNeedBootstrap.get(namespaceName);
      if (tablesInfoListNeedBootstrap.isEmpty()) {
        throw new RuntimeException(
            String.format(
                "tablesInfoListNeedBootstrap in namespaceName %s is empty", namespaceName));
      }
      CommonTypes.TableType tableType = tablesInfoListNeedBootstrap.get(0).getTableType();
      Set<String> tableIdsNeedBootstrap = getTableIds(tablesInfoListNeedBootstrap);
      log.info(
          "Creating subtasks to set up replication using bootstrap for tables {} in "
              + "keyspace {}",
          tableIdsNeedBootstrap,
          namespaceName);

      // Create checkpoints for the tables.
      createBootstrapProducerTask(tableIdsNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingProducer);

      boolean useYbc =
          sourceUniverse.isYbcEnabled()
              && targetUniverse.isYbcEnabled()
              && confGetter.getGlobalConf(GlobalConfKeys.enableYbcForXCluster);

      // Backup from the source universe.
      BackupRequestParams backupRequestParams =
          getBackupRequestParams(sourceUniverse, bootstrapParams, tablesInfoListNeedBootstrap);
      Backup backup =
          createAllBackupSubtasks(
              backupRequestParams, UserTaskDetails.SubTaskGroupType.CreatingBackup, useYbc);

      backupList.add(backup);

      // Assign the created backup UUID for the tables in the DB.
      xClusterConfig.updateBackupForTables(tableIdsNeedBootstrap, backup);

      // Before dropping the tables on the target universe, delete the associated PITR configs.
      Optional<PitrConfig> pitrConfigOptional =
          PitrConfig.maybeGet(xClusterConfig.getTargetUniverseUUID(), tableType, namespaceName);
      if (xClusterConfig.getType().equals(ConfigType.Txn)) {
        pitrConfigOptional.ifPresent(
            pitrConfig -> createDeletePitrConfigTask(pitrConfig.getUuid()));
      }

      if (tableType == CommonTypes.TableType.YQL_TABLE_TYPE) {
        // If the table type is YCQL, delete the tables from the target universe, because if the
        // tables exist, the restore subtask will fail.
        List<String> tableNamesNeedBootstrap =
            tablesInfoListNeedBootstrap.stream()
                .filter(
                    tableInfo ->
                        tableInfo.getRelationType()
                            != MasterTypes.RelationType.INDEX_TABLE_RELATION)
                .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                .collect(Collectors.toList());
        List<String> tableNamesToDeleteOnTargetUniverse =
            getTableInfoList(targetUniverse).stream()
                .filter(
                    tableInfo ->
                        tableType.equals(tableInfo.getTableType())
                            && tableNamesNeedBootstrap.contains(tableInfo.getName())
                            && tableInfo.getNamespace().getName().equals(namespaceName))
                .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
                .collect(Collectors.toList());
        createDeleteTablesFromUniverseTask(
                targetUniverse.getUniverseUUID(),
                Collections.singletonMap(namespaceName, tableNamesToDeleteOnTargetUniverse))
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);
      } else if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
        // If the table type is YSQL, delete the database from the target universe before restore.
        createDeleteKeySpaceTask(namespaceName, CommonTypes.TableType.PGSQL_TABLE_TYPE);
      }

      // Wait for sometime to make sure the above drop database has reached all the nodes.
      Duration waitTime =
          this.confGetter.getConfForScope(
              targetUniverse, UniverseConfKeys.sleepTimeBeforeRestoreXClusterSetup);
      if (waitTime.compareTo(Duration.ZERO) > 0) {
        createWaitForDurationSubtask(targetUniverse, waitTime)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);
      }

      // Restore to the target universe.
      RestoreBackupParams restoreBackupParams =
          getRestoreBackupParams(sourceUniverse, targetUniverse, backupRequestParams, backup);
      Restore restore =
          createAllRestoreSubtasks(
              restoreBackupParams, UserTaskDetails.SubTaskGroupType.RestoringBackup);
      restoreList.add(restore);

      // Assign the created restore UUID for the tables in the DB.
      xClusterConfig.updateRestoreForTables(tableIdsNeedBootstrap, restore);

      // Set the restore time for the tables in the DB.
      createSetRestoreTimeTask(tableIdsNeedBootstrap)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.RestoringBackup);

      // Recreate the PITR config for txn xCluster.
      if (xClusterConfig.getType().equals(ConfigType.Txn)) {
        if (xClusterConfig.isUsedForDr()) {
          // For DR, read the parameters from taskParams.
          if (Objects.isNull(taskParams().getPitrParams())) {
            throw new IllegalArgumentException(
                "pitrParams in taskParams cannot be null while creating an xCluster config for DR");
          }
          createCreatePitrConfigTask(
              namespaceName,
              tableType,
              taskParams().getPitrParams().retentionPeriodSec,
              xClusterConfig);
        } else if (pitrConfigOptional.isPresent()) {
          // Read the parameters from the last existing PITR config.
          createCreatePitrConfigTask(
              namespaceName,
              tableType,
              pitrConfigOptional.get().getRetentionPeriod(),
              xClusterConfig);
        } else {
          // Use default parameters.
          createCreatePitrConfigTask(
              namespaceName,
              tableType,
              confGetter
                  .getConfForScope(
                      targetUniverse, UniverseConfKeys.txnXClusterPitrDefaultRetentionPeriod)
                  .getSeconds(),
              xClusterConfig);
        }
      }

      if (isReplicationConfigCreated) {
        // If the xCluster config is already created, add the bootstrapped tables to the created
        // xCluster config.
        createXClusterConfigModifyTablesTask(
                tableIdsNeedBootstrap, XClusterConfigModifyTables.Params.Action.ADD)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
      } else {
        // Set up the replication config.
        createXClusterConfigSetupTask(tableIdsNeedBootstrap)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        isReplicationConfigCreated = true;
      }
    }
  }

  protected Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
      getDbToTablesInfoMapNeedBootstrap(
          Set<String> tableIds,
          List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
          Map<String, List<String>> mainTableIndexTablesMap,
          Set<String> sourceTableIdsWithNoTableOnTargetUniverse) {
    if (requestedTableInfoList.isEmpty()) {
      log.warn("requestedTablesInfoList is empty");
      return Collections.emptyMap();
    }
    // At least one entry exists in requestedTableInfoList.
    CommonTypes.TableType tableType = requestedTableInfoList.get(0).getTableType();
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    checkBootstrapRequiredForReplicationSetup(getTableIdsNeedBootstrap(tableIds));

    // If a table does not exist on the target universe, bootstrapping will be required for it.
    xClusterConfig.updateNeedBootstrapForTables(
        sourceTableIdsWithNoTableOnTargetUniverse, true /* needBootstrap */);

    Set<String> tableIdsNeedBootstrap = getTableIdsNeedBootstrap(tableIds);
    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>> dbToTableInfoListMap =
        groupByNamespaceName(requestedTableInfoList);
    dbToTableInfoListMap.forEach(
        (namespaceName, tablesInfoList) -> {
          Set<String> tableIdsInNamespace = getTableIds(tablesInfoList);
          // If at least one YSQL table needs bootstrap, it must be done for all tables in that
          // keyspace.
          if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE
              && !getTablesNeedBootstrap(tableIdsInNamespace).isEmpty()) {
            xClusterConfig.updateNeedBootstrapForTables(
                tableIdsInNamespace, true /* needBootstrap */);
          }
          // If a main table or an index table of a main table needs bootstrapping, it must be
          // done for the main table and all of its index tables due to backup/restore
          // restrictions.
          mainTableIndexTablesMap.forEach(
              (mainTableId, indexTableIds) -> {
                if (tableIdsNeedBootstrap.contains(mainTableId)
                    || indexTableIds.stream().anyMatch(tableIdsNeedBootstrap::contains)) {
                  xClusterConfig.updateNeedBootstrapForTables(
                      Collections.singleton(mainTableId), true /* needBootstrap */);
                  xClusterConfig.updateNeedBootstrapForTables(
                      indexTableIds, true /* needBootstrap */);
                }
              });
        });

    // Get tables requiring bootstrap again in case previous statement has made any changes.
    Set<String> tableIdsNeedBootstrapAfterChanges =
        getTableIdsNeedBootstrap(getTableIds(requestedTableInfoList));

    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        dbToTablesInfoMapNeedBootstrap =
            requestedTableInfoList.stream()
                .filter(
                    tableInfo ->
                        tableIdsNeedBootstrapAfterChanges.contains(
                            tableInfo.getId().toStringUtf8()))
                .collect(Collectors.groupingBy(tableInfo -> tableInfo.getNamespace().getName()));

    log.debug("dbToTablesInfoMapNeedBootstrap is {}", dbToTablesInfoMapNeedBootstrap);
    return dbToTablesInfoMapNeedBootstrap;
  }

  static BackupRequestParams getBackupRequestParams(
      Universe sourceUniverse,
      XClusterConfigCreateFormData.BootstrapParams bootstrapParams,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoListNeedBootstrap) {
    BackupRequestParams backupRequestParams;
    if (bootstrapParams != null && bootstrapParams.backupRequestParams != null) {
      backupRequestParams = new BackupRequestParams();
      backupRequestParams.storageConfigUUID = bootstrapParams.backupRequestParams.storageConfigUUID;
      backupRequestParams.parallelism = bootstrapParams.backupRequestParams.parallelism;
    } else {
      // In case the user does not pass the backup parameters, use the default values.
      backupRequestParams = new BackupRequestParams();
      backupRequestParams.customerUUID = Customer.get(sourceUniverse.getCustomerId()).getUuid();
      // Use the last storage config used for a successful backup as the default one.
      Optional<Backup> latestCompletedBackupOptional =
          Backup.fetchLatestByState(backupRequestParams.customerUUID, Backup.BackupState.Completed);
      if (!latestCompletedBackupOptional.isPresent()) {
        throw new RuntimeException(
            "bootstrapParams in XClusterConfigCreateFormData is null, and storageConfigUUID "
                + "cannot be determined based on the latest successful backup");
      }
      backupRequestParams.storageConfigUUID =
          latestCompletedBackupOptional.get().getStorageConfigUUID();
      log.info(
          "storageConfigUUID {} will be used for bootstrapping",
          backupRequestParams.storageConfigUUID);
    }
    // These parameters are pre-set. Others either come from the user, or the defaults are good.
    backupRequestParams.setUniverseUUID(sourceUniverse.getUniverseUUID());
    backupRequestParams.customerUUID = Customer.get(sourceUniverse.getCustomerId()).getUuid();
    backupRequestParams.backupType = tablesInfoListNeedBootstrap.get(0).getTableType();
    backupRequestParams.timeBeforeDelete = TIME_BEFORE_DELETE_BACKUP_MS;
    backupRequestParams.expiryTimeUnit = com.yugabyte.yw.models.helpers.TimeUnit.MILLISECONDS;
    // Set to true because it is a beta version of bootstrapping, and we need to debug it.
    backupRequestParams.enableVerboseLogs = true;
    // Ensure keyspaceTableList is not specified by the user.
    if (backupRequestParams.keyspaceTableList != null) {
      throw new RuntimeException(
          "backupRequestParams.keyspaceTableList must be null, table selection happens "
              + "automatically");
    }
    backupRequestParams.keyspaceTableList = new ArrayList<>();
    BackupRequestParams.KeyspaceTable keyspaceTable = new BackupRequestParams.KeyspaceTable();
    keyspaceTable.keyspace = tablesInfoListNeedBootstrap.get(0).getNamespace().getName();
    if (backupRequestParams.backupType != CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesNeedBootstrapInfoList =
          tablesInfoListNeedBootstrap.stream()
              .filter(
                  tableInfo ->
                      tableInfo.getRelationType() != MasterTypes.RelationType.INDEX_TABLE_RELATION)
              .collect(Collectors.toList());
      keyspaceTable.tableNameList =
          tablesNeedBootstrapInfoList.stream()
              .map(MasterDdlOuterClass.ListTablesResponsePB.TableInfo::getName)
              .collect(Collectors.toList());
      keyspaceTable.tableUUIDList =
          tablesNeedBootstrapInfoList.stream()
              .map(tableInfo -> Util.getUUIDRepresentation(tableInfo.getId().toStringUtf8()))
              .collect(Collectors.toList());
    }
    backupRequestParams.keyspaceTableList.add(keyspaceTable);
    return backupRequestParams;
  }

  static RestoreBackupParams getRestoreBackupParams(
      Universe sourceUniverse,
      Universe targetUniverse,
      BackupRequestParams backupRequestParams,
      Backup backup) {
    RestoreBackupParams restoreTaskParams = new RestoreBackupParams();
    // For the following parameters the default values will be used:
    //    restoreTaskParams.alterLoadBalancer = true
    //    restoreTaskParams.restoreTimeStamp = null
    //    public String oldOwner = "yugabyte"
    //    public String newOwner = null
    // The following parameters are set. For others, the defaults are good.
    restoreTaskParams.customerUUID = backupRequestParams.customerUUID;
    restoreTaskParams.setUniverseUUID(targetUniverse.getUniverseUUID());
    if (sourceUniverse.getUniverseDetails().encryptionAtRestConfig != null) {
      restoreTaskParams.kmsConfigUUID =
          sourceUniverse.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID;
    }
    restoreTaskParams.actionType = RestoreBackupParams.ActionType.RESTORE;
    restoreTaskParams.enableVerboseLogs = backupRequestParams.enableVerboseLogs;
    restoreTaskParams.storageConfigUUID = backupRequestParams.storageConfigUUID;
    restoreTaskParams.useTablespaces = backupRequestParams.useTablespaces;
    restoreTaskParams.parallelism = backupRequestParams.parallelism;
    restoreTaskParams.disableChecksum = backupRequestParams.disableChecksum;
    restoreTaskParams.category = backup.getCategory();
    restoreTaskParams.prefixUUID = UUID.randomUUID();
    // Set storage info.
    restoreTaskParams.backupStorageInfoList = new ArrayList<>();
    RestoreBackupParams.BackupStorageInfo backupStorageInfo =
        new RestoreBackupParams.BackupStorageInfo();
    backupStorageInfo.backupType = backupRequestParams.backupType;
    List<BackupTableParams> backupList = backup.getBackupParamsCollection();
    if (backupList == null) {
      throw new RuntimeException("backup.getBackupInfo().backupList must not be null");
    }
    if (backupList.size() != 1) {
      String errMsg =
          String.format(
              "backup.getBackupInfo().backupList must have exactly one element, had %d",
              backupList.size());
      throw new RuntimeException(errMsg);
    }
    backupStorageInfo.storageLocation = backupList.get(0).storageLocation;
    List<BackupRequestParams.KeyspaceTable> keyspaceTableList =
        backupRequestParams.keyspaceTableList;
    if (keyspaceTableList == null) {
      throw new RuntimeException("backupRequestParams.keyspaceTableList must not be null");
    }
    if (keyspaceTableList.size() != 1) {
      String errMsg =
          String.format(
              "backupRequestParams.keyspaceTableList must have exactly one element, had %d",
              keyspaceTableList.size());
      throw new RuntimeException(errMsg);
    }
    backupStorageInfo.keyspace = keyspaceTableList.get(0).keyspace;
    backupStorageInfo.sse = backupRequestParams.sse;
    restoreTaskParams.backupStorageInfoList.add(backupStorageInfo);

    return restoreTaskParams;
  }
}
