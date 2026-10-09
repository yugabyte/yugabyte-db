// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;
import com.yugabyte.PGConnection;
import com.yugabyte.replication.LogSequenceNumber;
import com.yugabyte.replication.PGReplicationConnection;
import com.yugabyte.replication.PGReplicationStream;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.PgOutputMessageDecoder.*;

@RunWith(value = YBTestRunner.class)
public class TestPgReplicationSlotWithTxnDdl extends BasePgSQLTest {
  private static final String YB_OUTPUT_PLUGIN_NAME =
      PgReplicationSlotTestUtil.YB_OUTPUT_PLUGIN_NAME;
  private static final int INT4_OID = 23;
  private static final int TEXT_OID = 25;

  @Override
  protected int getInitialNumTServers() {
    return PgReplicationSlotTestUtil.NUM_TSERVERS;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    PgReplicationSlotTestUtil.addCommonTServerFlags(flagMap);
    flagMap.put("TEST_ysql_yb_enable_replication_slot_transactional_ddl", "true");
    toggleDDLMode(flagMap, /* useLegacy */ false);
    flagMap.put("ysql_yb_enable_ddl_savepoint_support", "true");
    flagMap.put("vmodule", "cdcsdk_virtual_wal=4");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    PgReplicationSlotTestUtil.addCommonMasterFlags(flagMap);
    flagMap.put("TEST_ysql_yb_enable_replication_slot_transactional_ddl", "true");
    toggleDDLMode(flagMap, /* useLegacy */ false);
    flagMap.put("ysql_yb_enable_ddl_savepoint_support", "true");
    return flagMap;
  }

  private void createSlot(PGReplicationConnection replConnection, String slotName,
      String pluginName) throws Exception {
    PgReplicationSlotTestUtil.createSlot(replConnection, slotName, pluginName);
  }

  private List<PgOutputMessage> receiveMessage(PGReplicationStream stream, int count)
      throws Exception {
    return PgReplicationSlotTestUtil.receiveMessage(stream, count);
  }

  private PGReplicationStream startStream(PGReplicationConnection replConnection, String slotName,
      String publicationName) throws Exception {
    return replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", publicationName)
        .start();
  }

  private static PgOutputRelationMessage relation(String tableName,
      PgOutputRelationMessageColumn... columns) {
    return PgOutputRelationMessage.CreateForComparison(
        "public", tableName, 'c' /* replicaIdentity */, Arrays.asList(columns));
  }

  private static PgOutputRelationMessageColumn column(String name, int dataTypeOid) {
    return PgOutputRelationMessageColumn.CreateForComparison(name, dataTypeOid);
  }

  private static PgOutputInsertMessage insert(String... values) {
    List<PgOutputMessageTupleColumn> columnValues = new ArrayList<>();
    for (String value : values) {
      columnValues.add(new PgOutputMessageTupleColumnValue(value));
    }
    return PgOutputInsertMessage.CreateForComparison(
        new PgOutputMessageTuple((short) values.length, columnValues));
  }

  private static PgOutputBeginMessage beginMessage(int transactionId) {
    return PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf(0L), transactionId);
  }

  private static PgOutputCommitMessage commitMessage() {
    return PgOutputCommitMessage.CreateForComparison(
        LogSequenceNumber.valueOf(0L), LogSequenceNumber.valueOf(0L));
  }

  // Pinning LSNs is brittle: any extra catalog write shifts them. BEGIN and COMMIT of one
  // transaction carry the same commit LSN, the commit end LSN is greater, and the next
  // transaction starts after that.
  private static void assertMessages(
      List<PgOutputMessage> expected, List<PgOutputMessage> actual) {
    assertEquals(normalizeLsns(expected), normalizeLsns(actual));
    assertLsnsIncrease(actual);
  }

  private static List<PgOutputMessage> normalizeLsns(List<PgOutputMessage> messages) {
    List<PgOutputMessage> normalized = new ArrayList<>(messages.size());
    for (PgOutputMessage message : messages) {
      normalized.add(normalizeLsn(message));
    }
    return normalized;
  }

  private static PgOutputMessage normalizeLsn(PgOutputMessage message) {
    if (message instanceof PgOutputBeginMessage) {
      return beginMessage(((PgOutputBeginMessage) message).transactionId);
    }
    if (message instanceof PgOutputCommitMessage) {
      return commitMessage();
    }
    return message;
  }

  private static void assertLsnsIncrease(List<PgOutputMessage> messages) {
    LogSequenceNumber previous = null;
    LogSequenceNumber beginLsn = null;
    for (PgOutputMessage message : messages) {
      if (message instanceof PgOutputBeginMessage) {
        assertTrue("BEGIN before the previous transaction committed", beginLsn == null);
        beginLsn = ((PgOutputBeginMessage) message).finalLSN;
        previous = assertLsnAfter(previous, beginLsn);
      } else if (message instanceof PgOutputCommitMessage) {
        PgOutputCommitMessage commit = (PgOutputCommitMessage) message;
        assertTrue("COMMIT without BEGIN", beginLsn != null);
        assertEquals(beginLsn, commit.commitLSN);
        previous = assertLsnAfter(previous, commit.endLSN);
        beginLsn = null;
      }
    }
    assertTrue("Transaction BEGIN without COMMIT", beginLsn == null);
  }

  private static LogSequenceNumber assertLsnAfter(
      LogSequenceNumber previous, LogSequenceNumber current) {
    if (previous != null) {
      assertTrue(
          "Expected LSN " + current + " to be greater than " + previous,
          current.asLong() > previous.asLong());
    }
    return current;
  }

  // Adding a column in the middle of a transaction: the rows inserted before the DDL are decoded
  // with the old schema and the rows inserted after it with the new one.
  @Test
  public void addColumnInsideTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one')");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two', 20)");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    // BEGIN, RELATION, INSERT, RELATION, INSERT, COMMIT.
    List<PgOutputMessage> result = receiveMessage(stream, 6);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("1", "one"),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID)),
        insert("2", "two", "20"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // Dropping a column in the middle of a transaction.
  @Test
  public void dropColumnInsideTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text, c int)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one', 10)");
      stmt.execute("ALTER TABLE t1 DROP COLUMN c");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two')");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    List<PgOutputMessage> result = receiveMessage(stream, 6);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID)),
        insert("1", "one", "10"),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("2", "two"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // Multiple DDLs interleaved with DML in a single transaction.
  @Test
  public void multipleDdlsInsideTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text, c int)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one', 10)");
      stmt.execute("ALTER TABLE t1 ADD COLUMN d text");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two', 20, 'two_d')");
      stmt.execute("ALTER TABLE t1 DROP COLUMN c");
      stmt.execute("INSERT INTO t1 VALUES (3, 'three', 'three_d')");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    // BEGIN, 3 * (RELATION, INSERT), COMMIT.
    List<PgOutputMessage> result = receiveMessage(stream, 8);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID)),
        insert("1", "one", "10"),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID),
            column("d", TEXT_OID)),
        insert("2", "two", "20", "two_d"),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("d", TEXT_OID)),
        insert("3", "three", "three_d"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // A transaction whose DDL is rolled back: no change of the transaction is streamed and the
  // schema used for subsequent changes is the one from before the transaction.
  @Test
  public void rolledBackDdlInsideTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one')");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two', 20)");
      stmt.execute("ROLLBACK");

      stmt.execute("INSERT INTO t1 VALUES (3, 'three')");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    // BEGIN, RELATION, INSERT, COMMIT of the single committed transaction.
    List<PgOutputMessage> result = receiveMessage(stream, 4);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("3", "three"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // A DDL committed in its own transaction is picked up by the changes of the following
  // transactions.
  @Test
  public void ddlOnlyTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
      stmt.execute("COMMIT");

      stmt.execute("INSERT INTO t1 VALUES (1, 'one', 10)");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    List<PgOutputMessage> result = receiveMessage(stream, 4);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(3),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID)),
        insert("1", "one", "10"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // CREATE TABLE on a FOR ALL TABLES publication triggers a publication refresh mid-txn, and
  // DROP COLUMN on an already-published table produces a synthetic DDL. Both must be ordered
  // correctly with the surrounding DML of the same transaction.
  @Test
  public void createTableAndDropColumnInsideTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text, c int)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("CREATE TABLE t2 (a int primary key, b text)");
      stmt.execute("INSERT INTO t2 VALUES (1, 'new1')");
      stmt.execute("ALTER TABLE t1 DROP COLUMN c");
      stmt.execute("INSERT INTO t2 VALUES (2, 'new2')");
      stmt.execute("INSERT INTO t1 VALUES (1, 'old1')");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    // BEGIN, RELATION t2, INSERT t2, INSERT t2, RELATION t1 (post-drop), INSERT t1, COMMIT.
    List<PgOutputMessage> result = receiveMessage(stream, 7);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t2", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("1", "new1"),
        insert("2", "new2"),
        // Note that this relation message comes after the INSERT t2 messages, because it is sent
        // with the first DML on table t1 after the DROP COLUMN DDL.
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("1", "old1"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // A multi-shard transaction containing DDL, then a single-shard insert. The follow-up DML must
  // be decoded with the post-DDL schema, not the catalog snapshot used while replaying the DDL
  // transaction.
  @Test
  public void singleShardInsertAfterTransactionalDdl() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b int)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 1)");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
      stmt.execute("INSERT INTO t1 VALUES (2, 1, 1)");
      stmt.execute("COMMIT");

      stmt.execute("INSERT INTO t1 VALUES (3, 4, 5)");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    // BEGIN, RELATION, INSERT, RELATION, INSERT, COMMIT of the first transaction, then
    // BEGIN, INSERT, COMMIT of the next insert.
    List<PgOutputMessage> result = receiveMessage(stream, 9);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", INT4_OID)),
        insert("1", "1"),
        relation("t1", column("a", INT4_OID), column("b", INT4_OID), column("c", INT4_OID)),
        insert("2", "1", "1"),
        commitMessage(),
        beginMessage(3),
        insert("3", "4", "5"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  @Test
  public void fastPathInsertAfterStandaloneDdlFollowingTransactionalDdl() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one')");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
      stmt.execute("COMMIT");

      stmt.execute("ALTER TABLE t1 ADD COLUMN d int");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two', 20, 30)");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    // BEGIN, RELATION(a,b), INSERT, COMMIT of the DDL txn, then
    // BEGIN, RELATION(a,b,c,d), INSERT, COMMIT of the fast-path insert.
    List<PgOutputMessage> result = receiveMessage(stream, 8);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("1", "one"),
        commitMessage(),
        beginMessage(4),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID),
            column("d", INT4_OID)),
        insert("2", "two", "20", "30"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // Two DDLs back to back, with no DML between them. The first DML after both DDLs must be
  // decoded with the schema that includes both new columns.
  @Test
  public void backToBackAddColumnsInsideTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one')");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
      stmt.execute("ALTER TABLE t1 ADD COLUMN d int");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two', 20, 30)");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    List<PgOutputMessage> result = receiveMessage(stream, 6);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("1", "one"),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID),
            column("d", INT4_OID)),
        insert("2", "two", "20", "30"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // A DDL rolled back to a savepoint must not be visible. The subsequent committed ADD COLUMN
  // is. Historical reads do not pass savepoint metadata, so aborted-subtxn intents must not
  // leak into catalog snapshots used for later DMLs.
  @Test
  public void addColumnRolledBackToSavepoint() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one')");
      stmt.execute("SAVEPOINT s1");
      stmt.execute("ALTER TABLE t1 ADD COLUMN d int");
      stmt.execute("ROLLBACK TO SAVEPOINT s1");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two', 20)");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    List<PgOutputMessage> result = receiveMessage(stream, 6);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("1", "one"),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID)),
        insert("2", "two", "20"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // A large transaction with a DDL in the middle must still order the schema change correctly
  // after the reorder buffer spills to disk.
  @Test
  public void ddlInsideSpilledTransaction() throws Exception {
    final String defaultMemoryLimitKb = "4096";
    // Minimum allowed by FLAG_GE_VALUE_VALIDATOR(64); 4000 inserts exceed this.
    final String memoryLimitKb = "64";
    Set<HostAndPort> tServers = miniCluster.getTabletServers().keySet();
    for (HostAndPort tServer : tServers) {
      setServerFlag(tServer, "ysql_yb_reorderbuffer_max_memory_kb", memoryLimitKb);
    }

    try {
      try (Statement stmt = connection.createStatement()) {
        stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
        stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
      }

      Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
      PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
      createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

      final int numInsertsBeforeDdl = 2000;
      final int numInsertsAfterDdl = 2000;
      try (Statement stmt = connection.createStatement()) {
        stmt.execute("BEGIN");
        for (int i = 0; i < numInsertsBeforeDdl; i++) {
          stmt.execute(String.format("INSERT INTO t1 VALUES (%d, 'pre_%d')", i, i));
        }
        stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
        for (int i = 0; i < numInsertsAfterDdl; i++) {
          stmt.execute(String.format(
              "INSERT INTO t1 VALUES (%d, 'post_%d', %d)",
              numInsertsBeforeDdl + i, i, i));
        }
        stmt.execute("COMMIT");
      }

      PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
      // BEGIN, RELATION, 2000 INSERT, RELATION, 2000 INSERT, COMMIT.
      final int expectedMessageCount = 2 + numInsertsBeforeDdl + 1 + numInsertsAfterDdl + 1;
      List<PgOutputMessage> result = receiveMessage(stream, expectedMessageCount);

      List<PgOutputMessage> expectedResult = new ArrayList<>();
      // BEGIN + 2000 INSERT + 3 synthetic DDLs + 2000 INSERT + COMMIT.
      expectedResult.add(beginMessage(2));
      expectedResult.add(relation("t1", column("a", INT4_OID), column("b", TEXT_OID)));
      for (int i = 0; i < numInsertsBeforeDdl; i++) {
        expectedResult.add(insert(String.format("%d", i), String.format("pre_%d", i)));
      }
      expectedResult.add(relation("t1", column("a", INT4_OID), column("b", TEXT_OID),
          column("c", INT4_OID)));
      for (int i = 0; i < numInsertsAfterDdl; i++) {
        expectedResult.add(insert(
            String.format("%d", numInsertsBeforeDdl + i),
            String.format("post_%d", i),
            String.format("%d", i)));
      }
      expectedResult.add(commitMessage());
      assertMessages(expectedResult, result);

      try (Statement stmt = connection.createStatement();
           ResultSet rs = stmt.executeQuery(
               "SELECT spill_count FROM pg_stat_replication_slots "
                   + "WHERE slot_name = 'test_slot'")) {
        assertTrue(rs.next());
        assertTrue("Expected the transaction to spill to disk", rs.getLong("spill_count") > 0);
      }

      stream.close();
      conn.close();
    } finally {
      for (HostAndPort tServer : tServers) {
        setServerFlag(tServer, "ysql_yb_reorderbuffer_max_memory_kb", defaultMemoryLimitKb);
      }
    }
  }

  // DROP COLUMN, then a DML, then CREATE TABLE (publication refresh) with no further DDL on t1.
  // The insert after the refresh must still use the post-DROP schema.
  @Test
  public void dmlAfterPublicationRefreshFollowingDdl() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text, c int)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("ALTER TABLE t1 DROP COLUMN c");
      stmt.execute("INSERT INTO t1 VALUES (1, 'old1')");
      stmt.execute("CREATE TABLE t2 (a int primary key, b text)");
      stmt.execute("INSERT INTO t1 VALUES (2, 'old2')");
      stmt.execute("INSERT INTO t2 VALUES (1, 'new1')");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    List<PgOutputMessage> result = receiveMessage(stream, 7);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("1", "old1"),
        insert("2", "old2"),
        relation("t2", column("a", INT4_OID), column("b", TEXT_OID)),
        insert("1", "new1"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }

  // ADD COLUMN with a volatile default rewrites the table. The rewrite copies existing rows
  // onto new tablets, and those writes are CDC-visible as INSERTs in the same transaction
  // (unlike Postgres, which does not re-stream rows present before the rewrite).
  @Test
  public void addColumnWithVolatileDefaultInsideTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one')");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int DEFAULT random()");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two', 20)");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    // BEGIN, RELATION(a,b), INSERT(1,one), RELATION(a,b,c), rewrite INSERT of row 1,
    // RELATION(a,b,c) again after the rewrite's publication refresh, INSERT(2,two,20), COMMIT.
    List<PgOutputMessage> result = receiveMessage(stream, 8);

    assertEquals(beginMessage(2), normalizeLsn(result.get(0)));
    assertEquals(relation("t1", column("a", INT4_OID), column("b", TEXT_OID)), result.get(1));
    assertEquals(insert("1", "one"), result.get(2));
    assertEquals(
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID)),
        result.get(3));

    assertTrue(
        "Expected a rewrite INSERT of the pre-DDL row",
        result.get(4) instanceof PgOutputInsertMessage);
    PgOutputInsertMessage rewriteInsert = (PgOutputInsertMessage) result.get(4);
    assertEquals("Rewrite INSERT should have three columns", 3, rewriteInsert.tuple.numColumns);
    assertEquals(
        new PgOutputMessageTupleColumnValue("1"), rewriteInsert.tuple.columns.get(0));
    assertEquals(
        new PgOutputMessageTupleColumnValue("one"), rewriteInsert.tuple.columns.get(1));
    assertTrue(
        "Rewrite INSERT column c should be the materialized DEFAULT random()",
        rewriteInsert.tuple.columns.get(2) instanceof PgOutputMessageTupleColumnValue);

    assertEquals(
        relation("t1", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID)),
        result.get(5));
    assertEquals(insert("2", "two", "20"), result.get(6));
    assertEquals(commitMessage(), normalizeLsn(result.get(7)));
    assertLsnsIncrease(result);

    stream.close();
    conn.close();
  }

  // As part of START_REPLICATION, historical read context is established. Stopping the stream
  // must clear that context so ordinary SQL on the same connection sees live catalog and data
  // and can write.
  @Test
  public void liveQueriesAfterStreamingResetHistoricalReadContext() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES (1, 'one')");
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");
      stmt.execute("INSERT INTO t1 VALUES (2, 'two', 20)");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    assertLsnsIncrease(receiveMessage(stream, 6));
    stream.close();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO t1 VALUES (3, 'three', 30)");
    }

    try (Statement live = conn.createStatement()) {
      try (ResultSet rs = live.executeQuery("SELECT a, b, c FROM t1 ORDER BY a")) {
        ResultSetMetaData md = rs.getMetaData();
        assertEquals(
            "Live SQL after streaming must see the current schema, not a historical catalog "
                + "snapshot left over from walsender",
            3, md.getColumnCount());
        assertTrue(rs.next());
        assertEquals(1, rs.getInt("a"));
        assertEquals("one", rs.getString("b"));
        rs.getObject("c");
        assertTrue(rs.wasNull());
        assertTrue(rs.next());
        assertEquals(2, rs.getInt("a"));
        assertEquals("two", rs.getString("b"));
        assertEquals(20, rs.getInt("c"));
        assertTrue(rs.next());
        assertEquals(3, rs.getInt("a"));
        assertEquals("three", rs.getString("b"));
        assertEquals(30, rs.getInt("c"));
        assertFalse(rs.next());
      }

      live.execute("INSERT INTO t1 VALUES (4, 'four', 40)");
      try (ResultSet rs = live.executeQuery("SELECT COUNT(*) FROM t1")) {
        assertTrue(rs.next());
        assertEquals(4, rs.getInt(1));
      }
    }

    conn.close();
  }

  // Concurrent DROP COLUMN from another session after A's insert and before A's commit.
  // B waits on AccessExclusiveLock vs A's RowExclusiveLock, so CT_B is after CT_A.
  // Decoding must still emit the insert with columns a, b, c (schema at write time).
  @Test
  public void insertThenConcurrentDropColumnBeforeCommit() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t2 (a int primary key, b text, c int)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t2");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, "test_slot", YB_OUTPUT_PLUGIN_NAME);

    try (Connection connA = getConnectionBuilder().withTServer(0).connect();
         Statement stmtA = connA.createStatement()) {
      stmtA.execute("BEGIN");
      stmtA.execute("INSERT INTO t2 VALUES (1, 'one', 10)");

      AtomicReference<Exception> ddlError = new AtomicReference<>();
      Thread ddlThread = new Thread(() -> {
        try (Connection connB = getConnectionBuilder().withTServer(0).connect();
             Statement stmtB = connB.createStatement()) {
          stmtB.execute("ALTER TABLE t2 DROP COLUMN c");
        } catch (Exception e) {
          ddlError.set(e);
        }
      });
      ddlThread.start();
      Thread.sleep(1000);
      stmtA.execute("COMMIT");
      ddlThread.join(60_000);
      if (ddlThread.isAlive()) {
        fail("ALTER TABLE t2 DROP COLUMN c still blocked after the insert committed");
      }
      if (ddlError.get() != null) {
        throw ddlError.get();
      }
    }

    PGReplicationStream stream = startStream(replConnection, "test_slot", "pub");
    List<PgOutputMessage> result = receiveMessage(stream, 4);

    List<PgOutputMessage> expectedResult = Arrays.asList(
        beginMessage(2),
        relation("t2", column("a", INT4_OID), column("b", TEXT_OID), column("c", INT4_OID)),
        insert("1", "one", "10"),
        commitMessage());
    assertMessages(expectedResult, result);

    stream.close();
    conn.close();
  }
}
