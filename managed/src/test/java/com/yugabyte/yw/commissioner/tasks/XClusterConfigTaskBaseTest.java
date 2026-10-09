// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.PlatformServiceException;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
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
}
