// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.RestoreMapper;
import api.v2.models.RestoreInfo;
import api.v2.models.RestoreKeyspaceInfo;
import api.v2.models.RestoreState;
import api.v2.models.TableType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.Universe;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import org.junit.Test;

public class RestoreMapperTest extends FakeDBApplication {

  @Test
  public void toRestore_mapsAllInfoFields() {
    Customer customer = ModelFactory.testCustomer();
    Universe target = ModelFactory.createUniverse("target-univ", customer.getId());
    Universe source = ModelFactory.createUniverse("source-univ", customer.getId());

    Date created = new Date(1_700_000_000_000L);
    Date updated = new Date(1_700_000_100_000L);
    Date backupCreated = new Date(1_699_000_000_000L);

    UUID restoreUuid = UUID.randomUUID();
    com.yugabyte.yw.models.Restore restore = new com.yugabyte.yw.models.Restore();
    restore.setRestoreUUID(restoreUuid);
    restore.setCustomerUUID(customer.getUuid());
    restore.setUniverseUUID(target.getUniverseUUID());
    restore.setSourceUniverseUUID(source.getUniverseUUID());
    restore.setSourceUniverseName("source-univ");
    restore.setState(com.yugabyte.yw.models.Restore.State.Completed);
    restore.setBackupType(org.yb.CommonTypes.TableType.PGSQL_TABLE_TYPE);
    restore.setBackupCreatedOnDate(backupCreated);
    restore.setRestoreSizeInBytes(123_456L);
    restore.setCreateTime(created);
    restore.setUpdateTime(updated);

    api.v2.models.Restore out = RestoreMapper.INSTANCE.toRestore(restore);

    assertNotNull(out.getInfo());
    RestoreInfo info = out.getInfo();
    assertEquals(restoreUuid, info.getUuid());
    assertEquals(customer.getUuid(), info.getCustomerUuid());
    assertEquals(target.getUniverseUUID(), info.getUniverseUuid());
    assertEquals(source.getUniverseUUID(), info.getSourceUniverseUuid());
    assertEquals("source-univ", info.getSourceUniverseName());
    assertEquals(RestoreState.Completed, info.getState());
    // backup_type now resolves to the shared api.v2.models.TableType enum.
    assertEquals(TableType.PGSQL_TABLE_TYPE, info.getBackupType());
    assertEquals(123_456L, info.getRestoreSizeInBytes().longValue());
    assertEquals(
        OffsetDateTime.ofInstant(created.toInstant(), ZoneOffset.UTC), info.getCreateTime());
    assertEquals(
        OffsetDateTime.ofInstant(updated.toInstant(), ZoneOffset.UTC), info.getUpdateTime());
    assertEquals(
        OffsetDateTime.ofInstant(backupCreated.toInstant(), ZoneOffset.UTC),
        info.getBackupCreatedOnDate());
    // Derived via @AfterMapping, resolved from the DB.
    assertEquals("target-univ", info.getUniverseName());
    assertTrue(info.getIsSourceUniversePresent());
  }

  @Test
  public void toRestore_missingUniverses_yieldEmptyNameAndNotPresent() {
    com.yugabyte.yw.models.Restore restore = new com.yugabyte.yw.models.Restore();
    restore.setRestoreUUID(UUID.randomUUID());
    restore.setCustomerUUID(UUID.randomUUID());
    restore.setUniverseUUID(UUID.randomUUID());
    restore.setSourceUniverseUUID(UUID.randomUUID());
    restore.setState(com.yugabyte.yw.models.Restore.State.InProgress);
    restore.setBackupType(org.yb.CommonTypes.TableType.YQL_TABLE_TYPE);

    RestoreInfo info = RestoreMapper.INSTANCE.toRestore(restore).getInfo();

    assertEquals("", info.getUniverseName());
    assertFalse(info.getIsSourceUniversePresent());
    assertEquals(RestoreState.InProgress, info.getState());
    assertEquals(TableType.YQL_TABLE_TYPE, info.getBackupType());
  }

  @Test
  public void toRestore_nullSourceUniverse_notPresent() {
    com.yugabyte.yw.models.Restore restore = new com.yugabyte.yw.models.Restore();
    restore.setRestoreUUID(UUID.randomUUID());
    restore.setUniverseUUID(UUID.randomUUID());
    restore.setSourceUniverseUUID(null);

    RestoreInfo info = RestoreMapper.INSTANCE.toRestore(restore).getInfo();

    assertFalse(info.getIsSourceUniversePresent());
  }

  @Test
  public void toRestoreKeyspaceInfo_mapsFields() {
    UUID ksUuid = UUID.randomUUID();
    UUID restoreUuid = UUID.randomUUID();
    RestoreKeyspace ks = new RestoreKeyspace();
    ks.setUuid(ksUuid);
    ks.setRestoreUUID(restoreUuid);
    ks.setSourceKeyspace("src_ks");
    ks.setTargetKeyspace("tgt_ks");
    ks.setStorageLocation("s3://bucket/ks");
    ks.setTableNameList(List.of("t1", "t2"));
    ks.setState(RestoreKeyspace.State.Completed);

    RestoreKeyspaceInfo info = RestoreMapper.INSTANCE.toRestoreKeyspaceInfo(ks);

    assertEquals(ksUuid, info.getUuid());
    assertEquals(restoreUuid, info.getRestoreUuid());
    assertEquals("src_ks", info.getSourceKeyspace());
    assertEquals("tgt_ks", info.getTargetKeyspace());
    assertEquals("s3://bucket/ks", info.getStorageLocation());
    assertEquals(List.of("t1", "t2"), info.getTableNameList());
    assertEquals(RestoreState.Completed, info.getState());
  }
}
