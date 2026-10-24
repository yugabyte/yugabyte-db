// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.time.Duration;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.RelationType;

@RunWith(MockitoJUnitRunner.class)
public class XClusterConfigTaskBaseTest {

  @Test
  public void validateOutInboundReplicationTablesSameTables() {
    Set<String> outboundSourceTableIds =
        new HashSet<>(Arrays.asList("00004103000030008000000000004000"));
    Set<String> inboundSourceTableIds =
        new HashSet<>(Arrays.asList("00004103000030008000000000004000"));
    try {
      XClusterConfigTaskBase.validateOutInboundReplicationTables(
          outboundSourceTableIds, inboundSourceTableIds);
    } catch (PlatformServiceException e) {
      fail();
    }
  }

  @Test
  public void validateOutInboundReplicationTablesOnlyInOutbound() {
    Set<String> outboundSourceTableIds =
        new HashSet<>(
            Arrays.asList("00004103000030008000000000004000", "00004104000030008000000000004004"));
    Set<String> inboundSourceTableIds =
        new HashSet<>(Arrays.asList("00004103000030008000000000004000"));
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                XClusterConfigTaskBase.validateOutInboundReplicationTables(
                    outboundSourceTableIds, inboundSourceTableIds));
    assertTrue(exception.getMessage().contains("are in outbound replication but not inbound"));
  }

  @Test
  public void validateOutInboundReplicationTablesOnlyInInbound() {
    Set<String> outboundSourceTableIds =
        new HashSet<>(Arrays.asList("00004103000030008000000000004000"));
    Set<String> inboundSourceTableIds =
        new HashSet<>(
            Arrays.asList("00004103000030008000000000004000", "00004104000030008000000000004004"));
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                XClusterConfigTaskBase.validateOutInboundReplicationTables(
                    outboundSourceTableIds, inboundSourceTableIds));
    assertTrue(exception.getMessage().contains("are in inbound replication but not outbound"));
  }

  private static final String DB1_ID = "00003000000030008000000000000001";
  private static final String DB2_ID = "00003000000030008000000000000002";
  private static final String TABLE_ID = "000030af000030008000000000004000";
  private static final String INDEX_TABLE_ID = "000030af000030008000000000004001";
  private static final String MATVIEW_TABLE_ID = "000030af000030008000000000004002";
  private static final String DB2_MATVIEW_TABLE_ID = "000030af000030008000000000004003";

  private static MasterDdlOuterClass.ListTablesResponsePB.TableInfo buildTableInfo(
      String tableId, String tableName, RelationType relationType, String namespaceId) {
    return MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder()
        .setTableType(CommonTypes.TableType.PGSQL_TABLE_TYPE)
        .setId(ByteString.copyFromUtf8(tableId))
        .setName(tableName)
        .setRelationType(relationType)
        .setNamespace(
            MasterTypes.NamespaceIdentifierPB.newBuilder()
                .setName(namespaceId)
                .setId(ByteString.copyFromUtf8(namespaceId))
                .build())
        .build();
  }

  private static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList() {
    return List.of(
        buildTableInfo(TABLE_ID, "table_1", RelationType.USER_TABLE_RELATION, DB1_ID),
        buildTableInfo(INDEX_TABLE_ID, "index_1", RelationType.INDEX_TABLE_RELATION, DB1_ID),
        buildTableInfo(MATVIEW_TABLE_ID, "matview_1", RelationType.MATVIEW_TABLE_RELATION, DB1_ID),
        buildTableInfo(
            DB2_MATVIEW_TABLE_ID, "matview_2", RelationType.MATVIEW_TABLE_RELATION, DB2_ID));
  }

  @Test
  public void getSupportedTableRelationTypes() {
    assertFalse(
        XClusterConfigTaskBase.getSupportedTableRelationTypes(false /* matviewSupported */)
            .contains(RelationType.MATVIEW_TABLE_RELATION));
    assertTrue(
        XClusterConfigTaskBase.getSupportedTableRelationTypes(true /* matviewSupported */)
            .contains(RelationType.MATVIEW_TABLE_RELATION));
    // Everything supported without matviews stays supported with them.
    assertTrue(
        XClusterConfigTaskBase.getSupportedTableRelationTypes(true /* matviewSupported */)
            .containsAll(
                XClusterConfigTaskBase.getSupportedTableRelationTypes(
                    false /* matviewSupported */)));
  }

  @Test
  public void isXClusterSupportedMatview() {
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo matview =
        buildTableInfo(MATVIEW_TABLE_ID, "matview_1", RelationType.MATVIEW_TABLE_RELATION, DB1_ID);
    assertFalse(XClusterConfigTaskBase.isXClusterSupported(matview, false /* matviewSupported */));
    assertTrue(XClusterConfigTaskBase.isXClusterSupported(matview, true /* matviewSupported */));
    // The single argument overload keeps rejecting matviews.
    assertFalse(XClusterConfigTaskBase.isXClusterSupported(matview));
  }

  @Test
  public void isXClusterSupportedNormalTablesUnchanged() {
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo mainTable =
        buildTableInfo(TABLE_ID, "table_1", RelationType.USER_TABLE_RELATION, DB1_ID);
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo indexTable =
        buildTableInfo(INDEX_TABLE_ID, "index_1", RelationType.INDEX_TABLE_RELATION, DB1_ID);
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo systemTable =
        buildTableInfo(
            "000030af000030008000000000004008",
            "pg_class",
            RelationType.SYSTEM_TABLE_RELATION,
            DB1_ID);

    for (boolean matviewSupported : new boolean[] {true, false}) {
      assertTrue(XClusterConfigTaskBase.isXClusterSupported(mainTable, matviewSupported));
      assertTrue(XClusterConfigTaskBase.isXClusterSupported(indexTable, matviewSupported));
      assertFalse(XClusterConfigTaskBase.isXClusterSupported(systemTable, matviewSupported));
    }
  }

  @Test
  public void getTableIdsPartitionedByIsXClusterSupportedMatview() {
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList =
        List.of(
            buildTableInfo(TABLE_ID, "table_1", RelationType.USER_TABLE_RELATION, DB1_ID),
            buildTableInfo(
                MATVIEW_TABLE_ID, "matview_1", RelationType.MATVIEW_TABLE_RELATION, DB1_ID));

    Map<Boolean, List<String>> notSupported =
        XClusterConfigTaskBase.getTableIdsPartitionedByIsXClusterSupported(
            tableInfoList, false /* matviewSupported */);
    assertEquals(List.of(TABLE_ID), notSupported.get(true));
    assertEquals(List.of(MATVIEW_TABLE_ID), notSupported.get(false));

    Map<Boolean, List<String>> supported =
        XClusterConfigTaskBase.getTableIdsPartitionedByIsXClusterSupported(
            tableInfoList, true /* matviewSupported */);
    assertEquals(
        new HashSet<>(List.of(TABLE_ID, MATVIEW_TABLE_ID)), new HashSet<>(supported.get(true)));
    assertTrue(supported.get(false).isEmpty());
  }

  @Test
  public void getRequestedTableInfoListExcludesMatviewWhenNotSupported() {
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoList(
            Set.of(DB1_ID), sourceTableInfoList(), false /* matviewSupported */);
    assertEquals(
        new HashSet<>(List.of(TABLE_ID, INDEX_TABLE_ID)),
        XClusterConfigTaskBase.getTableIds(requestedTableInfoList));
  }

  @Test
  public void getRequestedTableInfoListIncludesMatviewWhenSupported() {
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        XClusterConfigTaskBase.getRequestedTableInfoList(
            Set.of(DB1_ID, DB2_ID), sourceTableInfoList(), true /* matviewSupported */);
    assertEquals(
        new HashSet<>(List.of(TABLE_ID, INDEX_TABLE_ID, MATVIEW_TABLE_ID, DB2_MATVIEW_TABLE_ID)),
        XClusterConfigTaskBase.getTableIds(requestedTableInfoList));
  }

  @Test
  public void getRequestedTableInfoListMatviewOnlyDatabase() {
    // A database whose only replication candidate is a materialized view is not found when
    // matviews cannot be replicated, and is found when they can.
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        sourceTableInfoList();
    assertThrows(
        IllegalArgumentException.class,
        () ->
            XClusterConfigTaskBase.getRequestedTableInfoList(
                Set.of(DB2_ID), sourceTableInfoList, false /* matviewSupported */));
    assertEquals(
        Set.of(DB2_MATVIEW_TABLE_ID),
        XClusterConfigTaskBase.getTableIds(
            XClusterConfigTaskBase.getRequestedTableInfoList(
                Set.of(DB2_ID), sourceTableInfoList, true /* matviewSupported */)));
  }

  private static final String DB = "db";
  private static final Duration MAX_LAG = Duration.ofMinutes(15);
  private static final long LOW_LAG_US = Duration.ofSeconds(5).toNanos() / 1000;
  private static final long HIGH_LAG_US = MAX_LAG.plusSeconds(1).toNanos() / 1000;

  private static XClusterTableConfig table(
      XClusterConfig config, String db, XClusterTableConfig.Status status) {
    XClusterTableConfig tableConfig = new XClusterTableConfig(config, UUID.randomUUID().toString());
    tableConfig.setStatus(status);
    TableInfoResp info =
        TableInfoResp.builder().tableID(tableConfig.getTableId()).keySpace(db).build();
    // tables that only exist on the target have no source table info
    if (status == XClusterTableConfig.Status.ExtraTableOnTarget) {
      tableConfig.setTargetTableInfo(info);
    } else {
      tableConfig.setSourceTableInfo(info);
    }
    return tableConfig;
  }

  private static XClusterConfig config(
      boolean automaticDdlMode, XClusterTableConfig.Status... statuses) {
    XClusterConfig config = new XClusterConfig();
    config.setType(ConfigType.Db);
    config.setAutomaticDdlMode(automaticDdlMode);
    Set<XClusterTableConfig> tables = new HashSet<>();
    tables.add(table(config, DB, XClusterTableConfig.Status.Running));
    for (XClusterTableConfig.Status status : statuses) {
      tables.add(table(config, DB, status));
    }
    config.setTables(tables);
    return config;
  }

  private static XClusterNamespaceConfig.Status namespaceStatus(
      XClusterConfig config,
      Supplier<Map<String, Long>> safeTimeLags,
      Supplier<Optional<Set<String>>> walAnchorTableIds) {
    XClusterNamespaceConfig namespaceConfig = new XClusterNamespaceConfig(config, "id");
    namespaceConfig.setStatus(XClusterNamespaceConfig.Status.Running);
    namespaceConfig.setSourceNamespaceInfo(NamespaceInfoResp.builder().name(DB).build());
    XClusterConfigTaskBase.setNamespaceStatusesFromTables(
        config, Map.of(DB, namespaceConfig), safeTimeLags, walAnchorTableIds, MAX_LAG);
    return namespaceConfig.getStatus();
  }

  private static XClusterNamespaceConfig.Status namespaceStatus(
      XClusterConfig config, Supplier<Map<String, Long>> safeTimeLags) {
    // the source doesn't create WAL anchor streams
    return namespaceStatus(config, safeTimeLags, Optional::empty);
  }

  private static Set<String> tableIdsWithStatus(
      XClusterConfig config, XClusterTableConfig.Status status) {
    return config.getTableDetailsWithStatus(status).stream()
        .map(XClusterTableConfig::getTableId)
        .collect(Collectors.toSet());
  }

  private static XClusterNamespaceConfig.Status namespaceStatus(
      XClusterConfig config, long safeTimeLagUs) {
    return namespaceStatus(config, () -> Map.of(DB, safeTimeLagUs));
  }

  @Test
  public void testNamespaceStatusWithDdlInProgress() {
    for (XClusterTableConfig.Status status :
        List.of(
            XClusterTableConfig.Status.ExtraTableOnSource,
            XClusterTableConfig.Status.DroppedFromSource,
            XClusterTableConfig.Status.ExtraTableOnTarget)) {
      assertEquals(
          status.toString(),
          XClusterNamespaceConfig.Status.Updating,
          namespaceStatus(config(true, status), LOW_LAG_US));
      // the DDL is stuck
      assertEquals(
          status.toString(),
          XClusterNamespaceConfig.Status.Error,
          namespaceStatus(config(true, status), HIGH_LAG_US));
      assertEquals(
          status.toString(),
          XClusterNamespaceConfig.Status.Error,
          namespaceStatus(config(false, status), LOW_LAG_US));
    }
  }

  @Test
  public void testNamespaceStatusWithDdlInProgressAndOtherBadTable() {
    XClusterConfig config =
        config(
            true, XClusterTableConfig.Status.ExtraTableOnSource, XClusterTableConfig.Status.Failed);
    assertEquals(XClusterNamespaceConfig.Status.Error, namespaceStatus(config, LOW_LAG_US));
  }

  @Test
  public void testNamespaceStatusWithDdlInProgressAndReplicationError() {
    XClusterConfig config = config(true, XClusterTableConfig.Status.ExtraTableOnSource);
    config.getTableDetails().stream()
        .filter(t -> t.getStatus() == XClusterTableConfig.Status.ExtraTableOnSource)
        .forEach(
            t ->
                t.getReplicationStatusErrors()
                    .add(XClusterTableConfig.ReplicationStatusError.SCHEMA_MISMATCH));
    assertEquals(XClusterNamespaceConfig.Status.Error, namespaceStatus(config, LOW_LAG_US));
  }

  @Test
  public void testNamespaceStatusWithDdlInProgressAndUnableToFetch() {
    XClusterConfig config =
        config(
            true,
            XClusterTableConfig.Status.ExtraTableOnSource,
            XClusterTableConfig.Status.UnableToFetch);
    assertEquals(XClusterNamespaceConfig.Status.Warning, namespaceStatus(config, LOW_LAG_US));
  }

  @Test
  public void testNamespaceStatusWithoutSafeTime() {
    XClusterConfig config = config(true, XClusterTableConfig.Status.ExtraTableOnSource);
    assertEquals(XClusterNamespaceConfig.Status.Error, namespaceStatus(config, Map::of));
    assertEquals(
        XClusterNamespaceConfig.Status.Error,
        namespaceStatus(
            config,
            () -> {
              throw new RuntimeException("target unavailable");
            }));
  }

  @Test
  public void testNamespaceStatusFetchesSafeTimeOnlyIfNeeded() {
    AtomicInteger calls = new AtomicInteger();
    Supplier<Map<String, Long>> safeTimeLags =
        () -> {
          calls.incrementAndGet();
          return Map.of(DB, LOW_LAG_US);
        };
    assertEquals(
        XClusterNamespaceConfig.Status.Running, namespaceStatus(config(true), safeTimeLags));
    assertEquals(
        XClusterNamespaceConfig.Status.Error,
        namespaceStatus(config(true, XClusterTableConfig.Status.Failed), safeTimeLags));
    assertEquals(0, calls.get());
  }

  @Test
  public void testNamespaceStatusWithDdlQueuePaused() {
    XClusterConfig config = config(true, XClusterTableConfig.Status.ExtraTableOnSource);
    // the ddl_queue table of the DB reports the paused DDL replication
    XClusterTableConfig ddlQueue = table(config, DB, XClusterTableConfig.Status.Error);
    ddlQueue
        .getReplicationStatusErrors()
        .add(XClusterTableConfig.ReplicationStatusError.DDL_QUEUE_PAUSED);
    Set<XClusterTableConfig> tables = new HashSet<>(config.getTableDetails());
    tables.add(ddlQueue);
    config.setTables(tables);

    assertEquals(XClusterNamespaceConfig.Status.Error, namespaceStatus(config, LOW_LAG_US));
  }

  @Test
  public void testDdlQueuePausedErrorCode() {
    assertEquals(
        XClusterTableConfig.ReplicationStatusError.DDL_QUEUE_PAUSED,
        XClusterTableConfig.ReplicationStatusError.fromErrorCode(
            CommonTypes.ReplicationErrorPb.REPLICATION_DDL_QUEUE_PAUSED));
  }

  @Test
  public void testNamespaceStatusWithWalAnchors() {
    XClusterConfig config = config(true, XClusterTableConfig.Status.ExtraTableOnSource);
    Set<String> newTableIds =
        tableIdsWithStatus(config, XClusterTableConfig.Status.ExtraTableOnSource);

    // the new table is waiting for its CREATE TABLE on the target
    assertEquals(
        XClusterNamespaceConfig.Status.Updating,
        namespaceStatus(
            config, () -> Map.of(DB, LOW_LAG_US), () -> Optional.of(Set.copyOf(newTableIds))));
    // the safe time lag still catches a CREATE TABLE that hangs on the target
    assertEquals(
        XClusterNamespaceConfig.Status.Error,
        namespaceStatus(
            config, () -> Map.of(DB, HIGH_LAG_US), () -> Optional.of(Set.copyOf(newTableIds))));
    // the table isn't new, e.g. adding it to replication failed
    assertEquals(
        XClusterNamespaceConfig.Status.Error,
        namespaceStatus(config, () -> Map.of(DB, LOW_LAG_US), () -> Optional.of(Set.of())));
    // without WAL anchor streams, only the safe time lag counts
    assertEquals(
        XClusterNamespaceConfig.Status.Updating,
        namespaceStatus(
            config,
            () -> Map.of(DB, LOW_LAG_US),
            () -> {
              throw new RuntimeException("source unavailable");
            }));
  }

  @Test
  public void testWalAnchorsOnlyCheckedForNewTables() {
    AtomicInteger calls = new AtomicInteger();
    Supplier<Optional<Set<String>>> walAnchorTableIds =
        () -> {
          calls.incrementAndGet();
          return Optional.of(Set.of());
        };
    for (XClusterTableConfig.Status status :
        List.of(
            XClusterTableConfig.Status.DroppedFromSource,
            XClusterTableConfig.Status.ExtraTableOnTarget)) {
      assertEquals(
          XClusterNamespaceConfig.Status.Updating,
          namespaceStatus(config(true, status), () -> Map.of(DB, LOW_LAG_US), walAnchorTableIds));
    }
    assertEquals(0, calls.get());
  }
}
