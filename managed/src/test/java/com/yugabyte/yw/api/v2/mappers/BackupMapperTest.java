// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.BackupMapper;
import api.v2.models.Backup;
import api.v2.models.BackupInfo;
import api.v2.models.BackupSpec;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.forms.backuprestore.BackupPointInTimeRestoreWindow;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import com.yugabyte.yw.models.BackupResp;
import com.yugabyte.yw.models.CommonBackupInfo;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import org.junit.Test;
import org.yb.CommonTypes.TableType;

public class BackupMapperTest {

  @Test
  public void toBackup_mapsSpecAndInfo() {
    UUID backupUuid = UUID.randomUUID();
    UUID baseUuid = UUID.randomUUID();
    UUID customerUuid = UUID.randomUUID();
    UUID universeUuid = UUID.randomUUID();
    UUID storageUuid = UUID.randomUUID();
    UUID taskUuid = UUID.randomUUID();
    Date createTime = new Date(1_700_000_000_000L);

    CommonBackupInfo common =
        CommonBackupInfo.builder()
            .backupUUID(backupUuid)
            .baseBackupUUID(baseUuid)
            .state(BackupState.Completed)
            .storageConfigUUID(storageUuid)
            .taskUUID(taskUuid)
            .createTime(createTime)
            .updateTime(createTime)
            .completionTime(createTime)
            .totalBackupSizeInBytes(1024L)
            .sse(true)
            .tableByTableBackup(false)
            .responseList(
                Set.of(
                    KeyspaceTablesList.builder()
                        .keyspace("yugabyte")
                        .allTables(true)
                        .defaultLocation("s3://bucket/backup")
                        .perRegionLocations(
                            java.util.List.of(
                                new BackupUtil.RegionLocations() {
                                  {
                                    REGION = "us-west";
                                    LOCATION = "s3://bucket/west";
                                    HOST_BASE = "s3.amazonaws.com";
                                  }
                                }))
                        .backupPointInTimeRestoreWindow(
                            new BackupPointInTimeRestoreWindow(100L, 200L))
                        .build()))
            .build();

    BackupResp source =
        BackupResp.builder()
            .customerUUID(customerUuid)
            .universeUUID(universeUuid)
            .universeName("test-universe")
            .onDemand(true)
            .isFullBackup(true)
            .backupType(TableType.PGSQL_TABLE_TYPE)
            .category(BackupCategory.YB_CONTROLLER)
            .storageConfigType(StorageConfigType.S3)
            .expiryTimeUnit(TimeUnit.DAYS)
            .commonBackupInfo(common)
            .hasIncrementalBackups(false)
            .lastBackupState(BackupState.Completed)
            .isStorageConfigPresent(true)
            .isUniversePresent(true)
            .fullChainSizeInBytes(2048L)
            .useTablespaces(true)
            .useRoles(false)
            .build();

    Backup out = BackupMapper.INSTANCE.toBackup(source);

    assertNotNull(out.getSpec());
    assertNotNull(out.getInfo());
    BackupSpec spec = out.getSpec();
    BackupInfo info = out.getInfo();

    assertEquals(customerUuid, spec.getCustomerUuid());
    assertEquals(universeUuid, spec.getUniverseUuid());
    assertEquals("test-universe", spec.getUniverseName());
    assertTrue(spec.getOnDemand());
    assertTrue(spec.getIsFullBackup());
    assertEquals(BackupSpec.BackupTypeEnum.PGSQL_TABLE_TYPE, spec.getBackupType());
    assertEquals(BackupSpec.CategoryEnum.CONTROLLER, spec.getCategory());
    assertEquals(BackupSpec.StorageConfigTypeEnum.S3, spec.getStorageConfigType());
    assertEquals(BackupSpec.ExpiryTimeUnitEnum.DAYS, spec.getExpiryTimeUnit());
    assertEquals(1, spec.getKeyspaceTables().size());
    assertEquals("yugabyte", spec.getKeyspaceTables().get(0).getKeyspace());
    assertEquals(
        "us-west", spec.getKeyspaceTables().get(0).getPerRegionLocations().get(0).getRegion());
    assertEquals(
        100L,
        spec.getKeyspaceTables()
            .get(0)
            .getBackupPointInTimeRestoreWindow()
            .getTimestampRetentionWindowStartMillis()
            .longValue());

    assertEquals(backupUuid, info.getUuid());
    assertEquals(baseUuid, info.getBaseBackupUuid());
    assertEquals(api.v2.models.BackupState.BackupCompleted, info.getState());
    assertEquals(storageUuid, info.getStorageConfigUuid());
    assertEquals(taskUuid, info.getTaskUuid());
    assertEquals(1024L, info.getTotalBackupSizeInBytes().longValue());
    assertTrue(info.getSse());
    assertFalse(info.getHasIncrementalBackups());
    assertEquals(api.v2.models.BackupState.BackupCompleted, info.getLastBackupState());
    assertTrue(info.getIsStorageConfigPresent());
    assertTrue(info.getIsUniversePresent());
    assertEquals(2048L, info.getFullChainSizeInBytes().longValue());
  }
}
