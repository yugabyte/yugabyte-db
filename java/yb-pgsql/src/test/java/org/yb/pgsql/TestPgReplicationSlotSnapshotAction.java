package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestPgReplicationSlotSnapshotAction extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgReplicationSlot.class);

  private static int kPublicationRefreshIntervalSec = 5;

  public enum SnapshotAction { USE_SNAPSHOT, EXPORT_SNAPSHOT, NOEXPORT_SNAPSHOT }

  @Override
  protected int getInitialNumTServers() {
    return 3;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    if (isTestRunningWithConnectionManager()) {
      flagMap.put("ysql_conn_mgr_stats_interval", "1");
    }
    flagMap.put(
        "vmodule", "cdc_service=4,cdcsdk_producer=4,ybc_pggate=4,cdcsdk_virtual_wal=4,client=4");
    flagMap.put("ysql_log_min_messages", "DEBUG2");
    flagMap.put(
        "cdcsdk_publication_list_refresh_interval_secs", "" + kPublicationRefreshIntervalSec);
    flagMap.put("cdc_send_null_before_image_if_not_exists", "true");

    String enable_pg_export_snapshot_gFlag = "ysql_enable_pg_export_snapshot";
    flagMap.put("allowed_preview_flags_csv", enable_pg_export_snapshot_gFlag);
    flagMap.put(enable_pg_export_snapshot_gFlag, "true");
    return flagMap;
  }

  static String createReplicatioSlotWithSnapshotOption(
      Statement statement, String slotName, SnapshotAction snapshotAction) throws Exception {
    ResultSet rs = statement.executeQuery(String.format(
        "CREATE_REPLICATION_SLOT %s LOGICAL yboutput %s", slotName, snapshotAction.name()));
    if (snapshotAction != SnapshotAction.EXPORT_SNAPSHOT) {
      return null;
    }
    assertTrue(rs.next());
    return rs.getString(3);
  }

  protected void SetupTable() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("DROP TABLE IF EXISTS t1");
      statement.execute("CREATE TABLE t1(col INT PRIMARY KEY, col0 INT)");
      statement.execute("INSERT INTO t1 VALUES (1, 100), (2, 101)");
    }
  }

  @Test
  public void createReplicationSlotNoExportSnapshot() throws Exception {
    SetupTable();
    try (Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
        Connection conn2 = getConnectionBuilder().withTServer(0).replicationConnect();
        Statement statement1 = conn1.createStatement();
        Statement statement2 = conn2.createStatement()) {
      statement1.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      createReplicatioSlotWithSnapshotOption(
          statement1, "rs_logical_no_export_snapshot", SnapshotAction.NOEXPORT_SNAPSHOT);

      // Insert values from other session;
      statement2.execute("INSERT INTO t1 VALUES(3, 102)");

      List<Row> expectedRows = getSortedRowList(statement2.executeQuery("SELECT * FROM t1"));

      // Since we are using NOEXPORT_SNAPSHOT option the values inserted via 2nd session
      // (statement2) should be visible to first session.
      ResultSet rs = statement1.executeQuery("SELECT * FROM t1");
      assertEquals(expectedRows, getSortedRowList(rs));
    }
  }

  @Test
  public void createReplicationSlotUseSnapshot() throws Exception {
    SetupTable();
    try (Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
        Connection conn2 = getConnectionBuilder().withTServer(0).replicationConnect();
        Statement statement1 = conn1.createStatement();
        Statement statement2 = conn2.createStatement()) {
      List<Row> expectedRows = getSortedRowList(statement1.executeQuery("SELECT * FROM t1"));

      statement1.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      createReplicatioSlotWithSnapshotOption(
          statement1, "rs_logical_use_snapshot", SnapshotAction.USE_SNAPSHOT);

      // Insert values from other session;
      statement2.execute("INSERT INTO t1 VALUES(3, 102)");

      // Since we are using USE_SNAPSHOT option the values inserted via 2nd session (statement2)
      // should not be visible to first session.
      ResultSet rs = statement1.executeQuery("SELECT * FROM t1");
      assertEquals(expectedRows, getSortedRowList(rs));
    }
  }

  @Test
  public void createReplicationSlotExportSnapshot() throws Exception {
    SetupTable();
    try (Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
        Connection conn2 = getConnectionBuilder().withTServer(0).replicationConnect();
        Statement statement1 = conn1.createStatement();
        Statement statement2 = conn2.createStatement()) {
      List<Row> expectedRows = getSortedRowList(statement1.executeQuery("SELECT * FROM t1"));

      String snapshotId = createReplicatioSlotWithSnapshotOption(
          statement1, "rs_logical_export_snapshot", SnapshotAction.EXPORT_SNAPSHOT);
      LOG.info(String.format("Snapshot Exported: %s", snapshotId));

      // Insert values from other session;
      statement2.execute("INSERT INTO t1 VALUES(3, 102)");

      statement2.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      statement2.execute(String.format("SET TRANSACTION SNAPSHOT '%s'", snapshotId));
      ResultSet rs = statement2.executeQuery("SELECT * FROM t1");
      // Since we are importing the snapshot which was exported before inserting (3, 102) so this
      // row  should not be visible in this transaction.
      assertEquals(expectedRows, getSortedRowList(rs));
    }
  }

  @Test
  public void testReplicationSlotExportSnapshotDeletion() throws Exception {
    SetupTable();
    try (Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
        Connection conn2 = getConnectionBuilder().withTServer(0).replicationConnect();
        Statement statement1 = conn1.createStatement();
        Statement statement2 = conn2.createStatement()) {
      String snapshotId = createReplicatioSlotWithSnapshotOption(
          statement1, "rs_logical_export_snapshot_delete", SnapshotAction.EXPORT_SNAPSHOT);
      LOG.info(String.format("Snapshot Exported: %s", snapshotId));

      // Running a query in same session after exporting snapshot should delete the exported
      // snapshot.
      statement1.execute("SELECT * FROM t1");

      statement2.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      runInvalidQuery(statement2, String.format("SET TRANSACTION SNAPSHOT '%s'", snapshotId),
          "Could not find Snapshot");
    }
  }

  @Test
  public void testReplicationSlotExportSnapshotInTxnBlock() throws Exception {
    SetupTable();
    try (Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
        Statement statement1 = conn1.createStatement();) {
      statement1.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      runInvalidQuery(statement1,
          "CREATE_REPLICATION_SLOT rs_logical_export_snapshot_txn LOGICAL yboutput EXPORT_SNAPSHOT",
          "CREATE_REPLICATION_SLOT ... (SNAPSHOT 'export') must not be called inside a "
              + "transaction");
    }
  }

  @Test
  public void testReplicationSlotUseSnapshotOutsideTxnBlock() throws Exception {
    SetupTable();
    try (Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
        Statement statement1 = conn1.createStatement();) {
      runInvalidQuery(statement1,
          "CREATE_REPLICATION_SLOT rs_logical_use_snapshot_txn LOGICAL yboutput USE_SNAPSHOT",
          "CREATE_REPLICATION_SLOT ... (SNAPSHOT 'use') must be called inside a transaction");
    }
  }
}
