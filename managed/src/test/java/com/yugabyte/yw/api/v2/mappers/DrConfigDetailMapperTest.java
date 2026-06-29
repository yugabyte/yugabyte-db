// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.DrConfigDetailMapper;
import api.v2.models.DrConfigDbDetail;
import api.v2.models.DrConfigReplicationDetailStatus;
import api.v2.models.DrConfigTableDetail;
import api.v2.models.TableInfo;
import api.v2.models.TableRelationType;
import api.v2.models.TableType;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.Test;
import org.yb.CommonTypes;
import org.yb.master.MasterTypes;

public class DrConfigDetailMapperTest {

  private static final String TABLE_ID = "000034d4000030008000000000004001";
  private static final UUID TABLE_UUID = UUID.fromString("000034d4-0000-3000-8000-000000004001");
  private static final String INDEX_TABLE_ID = "000034d4000030008000000000004005";
  private static final String NAMESPACE_ID = "111133df000030008000000000004006";
  private static final UUID RESTORE_UUID = UUID.fromString("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
  private static final UUID BACKUP_UUID = UUID.fromString("bbbbbbbb-cccc-dddd-eeee-ffffffffffff");

  @Test
  public void toTableInfo_mapsAllV1TableMetadataFields() {
    TableInfoResp source = sampleTableInfo();

    TableInfo mapped = DrConfigDetailMapper.INSTANCE.toTableInfo(source);

    assertEquals(TABLE_ID, mapped.getTableId());
    assertEquals(TABLE_UUID, mapped.getTableUuid());
    assertEquals("yugabyte", mapped.getKeyspace());
    assertEquals(TableType.YSQL, mapped.getTableType());
    assertEquals("towns", mapped.getTableName());
    assertEquals(TableRelationType.USER_TABLE_RELATION, mapped.getRelationType());
    assertEquals(Double.valueOf(0.0), mapped.getSizeBytes());
    assertEquals(Double.valueOf(0.0), mapped.getWalSizeBytes());
    assertFalse(mapped.getIndexTable());
    assertEquals(List.of(INDEX_TABLE_ID), mapped.getIndexTableIds());
    assertEquals("public", mapped.getPgSchemaName());
    assertFalse(mapped.getColocated());
  }

  @Test
  public void toTableDetail_mapsNestedTableInfoAndEmptyReplicationErrors() {
    XClusterConfig xClusterConfig = new XClusterConfig();
    xClusterConfig.setUuid(UUID.randomUUID());
    XClusterTableConfig source = new XClusterTableConfig(xClusterConfig, TABLE_ID);
    source.setStatus(XClusterTableConfig.Status.Running);
    source.setReplicationSetupDone(true);
    source.setStreamId("979b2e52b5bd22931047be309c773e95");
    source.setSourceTableInfo(sampleTableInfo());
    source.setTargetTableInfo(sampleTableInfo().toBuilder().tableID("target-table-id").build());
    source.setReplicationStatusErrors(Set.of());

    DrConfigTableDetail mapped = DrConfigDetailMapper.INSTANCE.toTableDetail(source);

    assertEquals(TABLE_ID, mapped.getTableId());
    assertEquals(DrConfigReplicationDetailStatus.REPLICATION_RUNNING, mapped.getStatus());
    assertTrue(mapped.getReplicationSetupDone());
    assertNotNull(mapped.getReplicationStatusErrors());
    assertTrue(mapped.getReplicationStatusErrors().isEmpty());
    assertNotNull(mapped.getSourceTableInfo());
    assertEquals("yugabyte", mapped.getSourceTableInfo().getKeyspace());
    assertEquals(
        TableRelationType.USER_TABLE_RELATION, mapped.getSourceTableInfo().getRelationType());
    assertNotNull(mapped.getTargetTableInfo());
    assertEquals("target-table-id", mapped.getTargetTableInfo().getTableId());
  }

  @Test
  public void toDbDetail_mapsRestoreAndBackupUuid() {
    XClusterConfig xClusterConfig = new XClusterConfig();
    xClusterConfig.setUuid(UUID.randomUUID());
    XClusterNamespaceConfig source = new XClusterNamespaceConfig(xClusterConfig, NAMESPACE_ID);
    source.setStatus(XClusterNamespaceConfig.Status.Running);
    com.yugabyte.yw.models.Restore restore = new com.yugabyte.yw.models.Restore();
    restore.setRestoreUUID(RESTORE_UUID);
    source.setRestore(restore);
    com.yugabyte.yw.models.Backup backup = new com.yugabyte.yw.models.Backup();
    backup.setBackupUUID(BACKUP_UUID);
    source.setBackup(backup);

    DrConfigDbDetail mapped = DrConfigDetailMapper.INSTANCE.toDbDetail(source);

    assertEquals(NAMESPACE_ID, mapped.getSourceNamespaceId());
    assertEquals(DrConfigReplicationDetailStatus.REPLICATION_RUNNING, mapped.getStatus());
    assertEquals(RESTORE_UUID, mapped.getRestoreUuid());
    assertEquals(BACKUP_UUID, mapped.getBackupUuid());
  }

  @Test
  public void applyReplicationDetailsFrom_copiesRestoreFromRefreshedRow() {
    XClusterConfig xClusterConfig = new XClusterConfig();
    xClusterConfig.setUuid(UUID.randomUUID());
    XClusterNamespaceConfig pagedRow = new XClusterNamespaceConfig(xClusterConfig, NAMESPACE_ID);
    XClusterNamespaceConfig refreshedRow =
        new XClusterNamespaceConfig(xClusterConfig, NAMESPACE_ID);
    com.yugabyte.yw.models.Restore restore = new com.yugabyte.yw.models.Restore();
    restore.setRestoreUUID(RESTORE_UUID);
    refreshedRow.setRestore(restore);

    pagedRow.applyReplicationDetailsFrom(refreshedRow);

    DrConfigDbDetail mapped = DrConfigDetailMapper.INSTANCE.toDbDetail(pagedRow);
    assertEquals(RESTORE_UUID, mapped.getRestoreUuid());
  }

  private static TableInfoResp sampleTableInfo() {
    return TableInfoResp.builder()
        .tableID(TABLE_ID)
        .tableUUID(TABLE_UUID)
        .keySpace("yugabyte")
        .tableType(CommonTypes.TableType.PGSQL_TABLE_TYPE)
        .tableName("towns")
        .relationType(MasterTypes.RelationType.USER_TABLE_RELATION)
        .sizeBytes(0.0)
        .walSizeBytes(0.0)
        .isIndexTable(false)
        .nameSpace(null)
        .tableSpace(null)
        .parentTableUUID(null)
        .mainTableUUID(null)
        .indexTableIDs(List.of(INDEX_TABLE_ID))
        .pgSchemaName("public")
        .colocated(false)
        .colocationParentId(null)
        .build();
  }
}
