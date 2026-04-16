package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
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

  private enum SnapshotAction { USE_SNAPSHOT, EXPORT_SNAPSHOT, NOEXPORT_SNAPSHOT }

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
        "cdcsdk_publication_list_refresh_interval_secs", Integer.toString(5));
    flagMap.put("cdc_send_null_before_image_if_not_exists", "true");

    return flagMap;
  }

  private static String createReplicationSlotQuery(
      String name, SnapshotAction action) throws Exception {
    return String.format(
        "CREATE_REPLICATION_SLOT %s LOGICAL yboutput %s", name, action.name());
  }

  private static String createReplicationSlot(
      Statement statement, String name, SnapshotAction action) throws Exception {
        ResultSet rs = statement.executeQuery(createReplicationSlotQuery(name, action));
        if (action != SnapshotAction.EXPORT_SNAPSHOT) {
          return null;
    }
    assertTrue(rs.next());
    return rs.getString(3);
  }

  private void SetupTable() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("DROP TABLE IF EXISTS t1;" +
        "CREATE TABLE t1(col INT PRIMARY KEY, col0 INT);" +
        "INSERT INTO t1 VALUES (1, 100), (2, 101)");
    }
  }

  private Connection connect() throws Exception {
    return getConnectionBuilder().replicationConnect();
  }

  private static List<Row> selectAllFromT1Sorted(Statement stmt) throws SQLException {
    return getSortedRowList(stmt.executeQuery("SELECT * FROM t1"));
  }

  private static void beginTxnRepeatableRead(Statement stmt) throws SQLException {
    stmt.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ");
  }

  @Test
  public void createReplicationSlotNoExportSnapshot() throws Exception {
    SetupTable();
    try (Connection conn1 = connect();
         Connection conn2 = connect();
         Statement statement1 = conn1.createStatement();
         Statement statement2 = conn2.createStatement()) {
      beginTxnRepeatableRead(statement1);
      createReplicationSlot(
          statement1, "rs_logical_no_export_snapshot", SnapshotAction.NOEXPORT_SNAPSHOT);

      // Insert values from other session;
      statement2.execute("INSERT INTO t1 VALUES(3, 102)");

      List<Row> expectedRows = selectAllFromT1Sorted(statement2);

      // Since we are using NOEXPORT_SNAPSHOT option the values inserted via 2nd session
      // (statement2) should be visible to first session.
      assertEquals(expectedRows, selectAllFromT1Sorted(statement1));
    }
  }

  @Test
  public void createReplicationSlotUseSnapshot() throws Exception {
    SetupTable();
    try (Connection conn1 = connect();
         Connection conn2 = connect();
         Statement statement1 = conn1.createStatement();
         Statement statement2 = conn2.createStatement()) {
      List<Row> expectedRows = selectAllFromT1Sorted(statement1);
      beginTxnRepeatableRead(statement1);
      createReplicationSlot(
          statement1, "rs_logical_use_snapshot", SnapshotAction.USE_SNAPSHOT);
      // Insert values from other session;
      statement2.execute("INSERT INTO t1 VALUES(3, 102)");
      // Since we are using USE_SNAPSHOT option the values inserted via 2nd session (statement2)
      // should not be visible to first session.
      assertEquals(expectedRows, selectAllFromT1Sorted(statement1));
    }
  }

  @Test
  public void testCreateReplicationSlotExportSnapshot() throws Exception {
    SetupTable();
    try (Connection conn1 = connect();
         Connection conn2 = connect();
         Statement statement1 = conn1.createStatement();
         Statement statement2 = conn2.createStatement()) {
      List<Row> expectedRows = selectAllFromT1Sorted(statement1);

      String snapshotId = createReplicationSlot(
          statement1, "rs_logical_export_snapshot", SnapshotAction.EXPORT_SNAPSHOT);
      LOG.info(String.format("Snapshot Exported: %s", snapshotId));

      // Insert values from other session;
      statement2.execute("INSERT INTO t1 VALUES(3, 102)");

      beginTxnRepeatableRead(statement2);
      statement2.execute(String.format("SET TRANSACTION SNAPSHOT '%s'", snapshotId));
      // Since we are importing the snapshot which was exported before inserting (3, 102) so this
      // row  should not be visible in this transaction.
      assertEquals(expectedRows, selectAllFromT1Sorted(statement2));
    }
  }

  @Test
  public void testReplicationSlotExportSnapshotDeletion() throws Exception {
    SetupTable();
    try (Connection conn1 = connect();
         Connection conn2 = connect();
         Statement statement1 = conn1.createStatement();
         Statement statement2 = conn2.createStatement()) {
          String snapshotId = createReplicationSlot(
          statement1, "rs_logical_export_snapshot_delete", SnapshotAction.EXPORT_SNAPSHOT);
          LOG.info(String.format("Snapshot Exported: %s", snapshotId));
      // Running a query in same session after exporting snapshot should delete the exported
      // snapshot.
      statement1.execute("SELECT * FROM t1");
      beginTxnRepeatableRead(statement2);
      runInvalidQuery(statement2, String.format("SET TRANSACTION SNAPSHOT '%s'", snapshotId),
          "Could not find Snapshot");
    }
  }

  @Test
  public void testReplicationSlotExportSnapshotInTxnBlock() throws Exception {
    SetupTable();
    try (Connection conn = connect();
         Statement statement = conn.createStatement();) {
      beginTxnRepeatableRead(statement);
      runInvalidQuery(statement,
          createReplicationSlotQuery(
              "rs_logical_export_snapshot_txn", SnapshotAction.EXPORT_SNAPSHOT),
          "CREATE_REPLICATION_SLOT ... (SNAPSHOT 'export') must not be called inside a "
              + "transaction");
    }
  }

  @Test
  public void testReplicationSlotUseSnapshotOutsideTxnBlock() throws Exception {
    SetupTable();
    try (Connection conn = connect();
         Statement statement = conn.createStatement();) {
      runInvalidQuery(statement,
          createReplicationSlotQuery("rs_logical_use_snapshot_txn", SnapshotAction.USE_SNAPSHOT),
          "CREATE_REPLICATION_SLOT ... (SNAPSHOT 'use') must be called inside a transaction");
    }
  }
}
