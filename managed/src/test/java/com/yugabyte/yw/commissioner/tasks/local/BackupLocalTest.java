// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.yb.CommonTypes.TableType;

@Slf4j
public class BackupLocalTest extends LocalProviderUniverseTestBase {

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(90, 120);
  }

  private BackupRequestParams getBackupParams(Universe universe, CustomerConfig customerConfig) {
    BackupRequestParams params = new BackupRequestParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.backupType = TableType.PGSQL_TABLE_TYPE;
    params.customerUUID = customer.getUuid();
    params.storageConfigUUID = customerConfig.getConfigUUID();
    params.timeBeforeDelete = 8640000L;
    params.expiryTimeUnit = TimeUnit.DAYS;

    return params;
  }

  private RestoreBackupParams getRestoreParams(
      Universe universe, Backup backup, CustomerConfig customerConfig, String keySpace) {
    RestoreBackupParams rParams = new RestoreBackupParams();
    rParams.setUniverseUUID(universe.getUniverseUUID());
    rParams.customerUUID = customer.getUuid();
    rParams.actionType = RestoreBackupParams.ActionType.RESTORE;
    rParams.storageConfigUUID = customerConfig.getConfigUUID();
    rParams.category = Backup.BackupCategory.YB_CONTROLLER;
    RestoreBackupParams.BackupStorageInfo storageInfo = new RestoreBackupParams.BackupStorageInfo();
    storageInfo.backupType = TableType.PGSQL_TABLE_TYPE;
    storageInfo.keyspace = keySpace;
    storageInfo.storageLocation = backup.getBackupInfo().backupList.get(0).storageLocation;
    List<RestoreBackupParams.BackupStorageInfo> storageInfoList = new ArrayList<>();
    storageInfoList.add(storageInfo);
    rParams.backupStorageInfoList = storageInfoList;
    rParams.category = Backup.BackupCategory.YB_CONTROLLER;

    return rParams;
  }

  @Test
  public void testYBCBackup() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe universe = createUniverseWithYbc(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());
    BackupRequestParams params = getBackupParams(universe, customerConfig);
    UUID taskUUID = backupHelper.createBackupTask(customer.getUuid(), params);
    TaskInfo taskInfo = waitForTask(taskUUID, universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    List<Backup> backups =
        Backup.fetchByUniverseUUID(customer.getUuid(), universe.getUniverseUUID());
    Backup backup = backups.get(0);

    // Restoring the backup on the same universe under a different keyspace.
    RestoreBackupParams rParams = getRestoreParams(universe, backup, customerConfig, "yb_restore");
    taskUUID = backupHelper.createRestoreTask(customer.getUuid(), rParams);
    taskInfo = waitForTask(taskUUID, universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyYSQL(universe);
    verifyYSQL(universe, false, "yb_restore");
    verifyPayload();
  }

  @Test
  public void testYBCBackuponDifferentUniverse() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent("universe-1", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());
    BackupRequestParams params = getBackupParams(source, customerConfig);
    UUID taskUUID = backupHelper.createBackupTask(customer.getUuid(), params);
    TaskInfo taskInfo = waitForTask(taskUUID, source);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    List<Backup> backups = Backup.fetchByUniverseUUID(customer.getUuid(), source.getUniverseUUID());
    Backup backup = backups.get(0);

    userIntent = getDefaultUserIntent("universe-2", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Restoring the backup on the same universe under a different keyspace.
    RestoreBackupParams rParams = getRestoreParams(target, backup, customerConfig, YUGABYTE_DB);
    taskUUID = backupHelper.createRestoreTask(customer.getUuid(), rParams);
    taskInfo = waitForTask(taskUUID, source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyYSQL(target);
    verifyPayload();
  }
}
