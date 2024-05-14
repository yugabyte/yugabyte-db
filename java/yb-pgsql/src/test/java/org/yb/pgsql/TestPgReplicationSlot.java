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
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.nio.ByteBuffer;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;

import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.pgsql.PgOutputMessageDecoder.*;

import com.yugabyte.PGConnection;
import com.yugabyte.replication.LogSequenceNumber;
import com.yugabyte.replication.PGReplicationConnection;
import com.yugabyte.replication.PGReplicationStream;
import com.yugabyte.util.PSQLException;

@RunWith(value = YBTestRunner.class)
public class TestPgReplicationSlot extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgReplicationSlot.class);
  private static int kMaxClockSkewMs = 100;

  private static int kPublicationRefreshIntervalSec = 5;

  @Override
  protected int getInitialNumTServers() {
    return 3;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("allowed_preview_flags_csv",
        "ysql_yb_enable_replication_commands," +
        "yb_enable_cdc_consistent_snapshot_streams," +
        "ysql_yb_enable_replica_identity");
    flagMap.put("ysql_yb_enable_replication_commands", "true");
    flagMap.put("ysql_TEST_enable_replication_slot_consumption", "true");
    flagMap.put("yb_enable_cdc_consistent_snapshot_streams", "true");
    flagMap.put("ysql_yb_enable_replica_identity", "true");
    flagMap.put(
        "vmodule", "cdc_service=4,cdcsdk_producer=4,ybc_pggate=4,cdcsdk_virtual_wal=4,client=4");
    flagMap.put("max_clock_skew_usec", "" + kMaxClockSkewMs * 1000);
    flagMap.put("ysql_log_min_messages", "DEBUG1");
    flagMap.put(
        "cdcsdk_publication_list_refresh_interval_secs","" + kPublicationRefreshIntervalSec);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("allowed_preview_flags_csv",
        "ysql_yb_enable_replication_commands," +
        "yb_enable_cdc_consistent_snapshot_streams," +
        "ysql_yb_enable_replica_identity");
    flagMap.put("ysql_yb_enable_replication_commands", "true");
    flagMap.put("ysql_TEST_enable_replication_slot_consumption", "true");
    flagMap.put("yb_enable_cdc_consistent_snapshot_streams", "true");
    flagMap.put("ysql_yb_enable_replica_identity", "true");
    flagMap.put("max_clock_skew_usec", "" + kMaxClockSkewMs * 1000);
    return flagMap;
  }

  void waitForSnapshotTimeToPass() throws Exception {
    // When a slot (stream) is created, we choose the current time as the consistent snapshot time
    // to tackle clock skew. This time could be `max_clock_skew_usec` in the future. Any inserts
    // done before this time could end up being part of the snapshot instead of the changes. This is
    // not a correctness issue and just an unintuitive behavior.
    //
    // In the tests, we want to wait for this time to pass, so that any DMLs we do end up being part
    // of the changes and not the snapshot.
    Thread.sleep(kMaxClockSkewMs);
  }

  void createStreamAndWaitForSnapshotTimeToPass(
      PGReplicationConnection replConnection, String slotName, String pluginName) throws Exception {
    replConnection.createReplicationSlot()
        .logical()
        .withSlotName(slotName)
        .withOutputPlugin(pluginName)
        .make();

    waitForSnapshotTimeToPass();
  }

  @Test
  public void createAndDropFromDifferentTservers() throws Exception {
    Connection conn1 = getConnectionBuilder().withTServer(0).connect();
    Connection conn2 = getConnectionBuilder().withTServer(1).connect();

    try (Statement statement = conn1.createStatement()) {
      statement.execute("select pg_create_logical_replication_slot('test_slot', 'pgoutput')");
    }
    try (Statement statement = conn2.createStatement()) {
      statement.execute("select pg_drop_replication_slot('test_slot')");
    }
    try (Statement statement = conn1.createStatement()) {
      statement.execute("select pg_create_logical_replication_slot('test_slot', 'pgoutput')");
    }
    try (Statement statement = conn2.createStatement()) {
      statement.execute("select pg_drop_replication_slot('test_slot')");
    }
  }

  @Test
  public void replicationConnectionCreateDrop() throws Exception {
    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    replConnection.createReplicationSlot()
        .logical()
        .withSlotName("test_slot_repl_conn")
        .withOutputPlugin("pgoutput")
        .make();
    replConnection.dropReplicationSlot("test_slot_repl_conn");
  }

  @Test
  public void replicationConnectionCreateTemporaryUnsupported() throws Exception {
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    String expectedErrorMessage = "Temporary replication slot is not yet supported";

    boolean exceptionThrown = false;
    try {
      replConnection.createReplicationSlot()
          .logical()
          .withSlotName("test_slot_repl_conn_temporary")
          .withOutputPlugin("pgoutput")
          .withTemporaryOption()
          .make();
    } catch (PSQLException e) {
      exceptionThrown = true;
      if (StringUtils.containsIgnoreCase(e.getMessage(), expectedErrorMessage)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
            e.getMessage(), expectedErrorMessage));
      }
    }

    assertTrue("Expected an exception but wasn't thrown", exceptionThrown);
  }

  @Test
  public void replicationConnectionCreatePhysicalUnsupported() throws Exception {
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    String expectedErrorMessage = "YSQL only supports logical replication slots";

    boolean exceptionThrown = false;
    try {
      replConnection.createReplicationSlot()
          .physical()
          .withSlotName("test_slot_repl_conn_temporary")
          .make();
    } catch (PSQLException e) {
      exceptionThrown = true;
      if (StringUtils.containsIgnoreCase(e.getMessage(), expectedErrorMessage)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
            e.getMessage(), expectedErrorMessage));
      }
    }

    assertTrue("Expected an exception but wasn't thrown", exceptionThrown);
  }

  private List<PgOutputMessage> receiveMessage(PGReplicationStream stream, int count)
      throws Exception {
    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>(count);
    for (int index = 0; index < count; index++) {
      PgOutputMessage message = PgOutputMessageDecoder.DecodeBytes(stream.read());
      result.add(message);
      LOG.info("Row = {}", message);
    }

    return result;
  }

  private static String toString(ByteBuffer buffer) {
    int offset = buffer.arrayOffset();
    byte[] source = buffer.array();
    int length = source.length - offset;

    return new String(source, offset, length);
  }

  private List<String> receiveTestDecodingMessages(PGReplicationStream stream, int count)
      throws Exception {
    List<String> result = new ArrayList<String>(count);
    for (int index = 0; index < count; index++) {
      String message = toString(stream.read());
      result.add(message);
      LOG.info("Row = {}", message);
    }

    return result;
  }

  // TODO(#20726): Add more test cases covering:
  // 1. INSERTs in a BEGIN/COMMIT block
  // 2. Single shard transactions
  // 3. Transactions with savepoints (commit/abort subtxns)
  // 4. Transactions after table rewrite operations like ADD PRIMARY KEY
  // 5. Add a table with REPLICA IDENTITY NOTHING.

  void testReplicationConnectionConsumption(String slotName) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("DROP TABLE IF EXISTS t3");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text, c bool)");
      stmt.execute("CREATE TABLE t2 (a int primary key, b text, c bool)");
      stmt.execute("CREATE TABLE t3 (a int primary key, b text, c bool)");

      // CHANGE is the default but we do it explicitly so that the tests do not need changing if we
      // change the default.
      stmt.execute("ALTER TABLE t1 REPLICA IDENTITY CHANGE");
      stmt.execute("ALTER TABLE t2 REPLICA IDENTITY DEFAULT");
      stmt.execute("ALTER TABLE t3 REPLICA IDENTITY FULL");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      // Do more than 2 DMLs, since replicationConnectionConsumptionMultipleBatches tests the
      // case when #records > cdcsdk_max_consistent_records.
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd', true)");
      stmt.execute("INSERT INTO t1 VALUES(2, 'defg', true)");
      stmt.execute("INSERT INTO t1 VALUES(3, 'hijk', false)");
      stmt.execute("INSERT INTO t2 VALUES(1, 'abcd', true)");
      stmt.execute("INSERT INTO t2 VALUES(2, 'defg', true)");
      stmt.execute("INSERT INTO t3 VALUES(1, 'abcd', true)");
      stmt.execute("INSERT INTO t3 VALUES(2, 'defg', true)");

      stmt.execute("UPDATE t1 SET b = 'updated_abcd' WHERE a = 1");
      stmt.execute("UPDATE t1 SET b = NULL, c = false WHERE a = 2");
      stmt.execute("UPDATE t2 SET b = 'updated_abcd' WHERE a = 1");
      stmt.execute("UPDATE t2 SET b = NULL, c = false WHERE a = 2");
      stmt.execute("UPDATE t3 SET b = 'updated_abcd' WHERE a = 1");
      stmt.execute("UPDATE t3 SET b = NULL, c = false WHERE a = 2");

      stmt.execute("DELETE FROM t1 WHERE a = 2");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 3 Relation, 7 * 3 (begin, insert and commit), 3 * 3 * 2 (begin, update and commit), 1 * 3
    // (begin, delete, commit).
    result.addAll(receiveMessage(stream, 45));

    // LSN Values of change records start from 2 in YSQL. LSN 1 is reserved for all snapshot
    // records.
    // Note that the LSN value passed in the BEGIN message is the commit_lsn of the
    // transaction, so it is set to "0/4" in this case as the first transaction contains the
    // following records: BEGIN(2) RELATION(NO LSN) INSERT(3) COMMIT(4).
    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c' /* replicaIdentity */,
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25),
                PgOutputRelationMessageColumn.CreateForComparison("c", 16))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd"),
                new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/7"), 3));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("defg"),
                new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/7"), LogSequenceNumber.valueOf("0/8")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 4));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("hijk"),
                new PgOutputMessageTupleColumnValue("f")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/D"), 5));
        add(PgOutputRelationMessage.CreateForComparison("public", "t2", 'd' /* replicaIdentity */,
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25),
                PgOutputRelationMessageColumn.CreateForComparison("c", 16))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd"),
                new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/D"), LogSequenceNumber.valueOf("0/E")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/10"), 6));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("defg"),
                new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/10"), LogSequenceNumber.valueOf("0/11")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/13"), 7));
        add(PgOutputRelationMessage.CreateForComparison("public", "t3", 'f' /* replicaIdentity */,
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25),
                PgOutputRelationMessageColumn.CreateForComparison("c", 16))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd"),
                new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/13"), LogSequenceNumber.valueOf("0/14")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/16"), 8));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("defg"),
                new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/16"), LogSequenceNumber.valueOf("0/17")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/19"), 9));
        add(PgOutputUpdateMessage.CreateForComparison(
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    // No before image in CHANGE, so all columns come as Toasted.
                    new PgOutputMessageTupleColumnToasted(),
                    new PgOutputMessageTupleColumnToasted(),
                    new PgOutputMessageTupleColumnToasted())),
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    new PgOutputMessageTupleColumnValue("1"),
                    new PgOutputMessageTupleColumnValue("updated_abcd"),
                    // Column 'c' was not modified, so it is sent as untouched toasted.
                    new PgOutputMessageTupleColumnToasted()))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/19"), LogSequenceNumber.valueOf("0/1A")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/1C"), 10));
        add(PgOutputUpdateMessage.CreateForComparison(
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    // No before image in CHANGE, so all columns come as Toasted.
                    new PgOutputMessageTupleColumnToasted(),
                    new PgOutputMessageTupleColumnToasted(),
                    new PgOutputMessageTupleColumnToasted())),
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    new PgOutputMessageTupleColumnValue("2"),
                    // Column 'b' was explicitly set to NULL, so it is sent as NULL.
                    new PgOutputMessageTupleColumnNull(),
                    new PgOutputMessageTupleColumnValue("f")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/1C"), LogSequenceNumber.valueOf("0/1D")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/1F"), 11));
        add(PgOutputUpdateMessage.CreateForComparison(
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    // No before image in DEFAULT, so all columns come as NULL, same as in PG.
                    new PgOutputMessageTupleColumnNull(),
                    new PgOutputMessageTupleColumnNull(),
                    new PgOutputMessageTupleColumnNull())),
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    new PgOutputMessageTupleColumnValue("1"),
                    new PgOutputMessageTupleColumnValue("updated_abcd"),
                    // Even though column 'c' was not modified, all columns are sent in DEFAULT.
                    new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/1F"), LogSequenceNumber.valueOf("0/20")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/22"), 12));
        add(PgOutputUpdateMessage.CreateForComparison(
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    // No before image in DEFAULT, so all columns come as NULL, same as in PG.
                    new PgOutputMessageTupleColumnNull(),
                    new PgOutputMessageTupleColumnNull(),
                    new PgOutputMessageTupleColumnNull())),
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    new PgOutputMessageTupleColumnValue("2"),
                    new PgOutputMessageTupleColumnNull(),
                    new PgOutputMessageTupleColumnValue("f")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/22"), LogSequenceNumber.valueOf("0/23")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/25"), 13));
        add(PgOutputUpdateMessage.CreateForComparison(
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    // All columns for before image in FULL, same as in PG.
                    new PgOutputMessageTupleColumnValue("1"),
                    new PgOutputMessageTupleColumnValue("abcd"),
                    new PgOutputMessageTupleColumnValue("t"))),
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    new PgOutputMessageTupleColumnValue("1"),
                    new PgOutputMessageTupleColumnValue("updated_abcd"),
                    new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/25"), LogSequenceNumber.valueOf("0/26")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/28"), 14));
        add(PgOutputUpdateMessage.CreateForComparison(
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    // All columns for before image in FULL, same as in PG.
                    new PgOutputMessageTupleColumnValue("2"),
                    new PgOutputMessageTupleColumnValue("defg"),
                    new PgOutputMessageTupleColumnValue("t"))),
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    new PgOutputMessageTupleColumnValue("2"),
                    new PgOutputMessageTupleColumnNull(),
                    new PgOutputMessageTupleColumnValue("f")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/28"), LogSequenceNumber.valueOf("0/29")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/2B"), 15));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
            new PgOutputMessageTuple((short) 3,
                Arrays.asList(
                    new PgOutputMessageTupleColumnValue("2"),
                    new PgOutputMessageTupleColumnNull(),
                    new PgOutputMessageTupleColumnNull()))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/2B"), LogSequenceNumber.valueOf("0/2C")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  // PG converts timestamp according to the local timezone before streaming via logical replication.
  // So we convert the timestamp to local timezone before the string comparison, so that the tests
  // doesn't depend on the machine timezone.
  String convertTimestampToSystemTimezone(String ts) {
    Instant instant = Instant.parse(ts);
    ZoneId zoneId = ZoneId.systemDefault();
    ZonedDateTime zonedDateTime = instant.atZone(zoneId);

    DateTimeFormatter outputFormatter;
    if (zonedDateTime.getOffset().equals(ZoneOffset.UTC)) {
      // For UTC, PG only prints 3 characters (+00) instead of (+00:00). So we handle that
      // separately. See EncodeTimezone in datetime.c for reference.
      outputFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss+00");
    } else {
      outputFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ssxxx");
    }
    return zonedDateTime.format(outputFormatter);
  }

  @Test
  public void testDynamicTableAdditionForAllTablesPublication() throws Exception {
    String slotName = "test_dynamic_table_addition_slot";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("DROP TABLE IF EXISTS t3");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t2 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
      getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");

    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO t2 VALUES(2, 'defg')");
      stmt.execute("COMMIT");
      stmt.execute("CREATE TABLE t3 (a int primary key, b text)");
    }

    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(3, 'mnop')");
      stmt.execute("INSERT INTO t2 VALUES(4, 'qrst')");
      stmt.execute("INSERT INTO t3 values(5, 'uvwx')");
      stmt.execute("COMMIT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 4 from first txn and 5 from second txn.
    result.addAll(receiveMessage(stream, 12));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        // begin
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/5"), 2));
        // insert 1
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnValue("abcd")))));
        // insert 2
        add(PgOutputRelationMessage.CreateForComparison("public", "t2", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("2"),
            new PgOutputMessageTupleColumnValue("defg")))));
        // commit
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/5"), LogSequenceNumber.valueOf("0/6")));

        // begin
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 3));
        // insert 1
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("3"),
            new PgOutputMessageTupleColumnValue("mnop")))));
        // insert 2
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("4"),
            new PgOutputMessageTupleColumnValue("qrst")))));
        // insert 3
        add(PgOutputRelationMessage.CreateForComparison("public", "t3", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("5"),
            new PgOutputMessageTupleColumnValue("uvwx")))));
        // commit
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));
      }
    };

    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testDynamicTableAdditionForTablesCreatedBeforeStreamCreation() throws Exception {
    String slotName = "test_dynamic_table_addition_slot";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("DROP TABLE IF EXISTS t3");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t2 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t3 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1, t2");
    }

    Connection conn =
      getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO t2 VALUES(2, 'defg')");
      stmt.execute("INSERT INTO t3 values(3, 'ghij')");
      stmt.execute("COMMIT");
      stmt.execute("ALTER PUBLICATION pub ADD TABLE t3");
    }

    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(3, 'mnop')");
      stmt.execute("INSERT INTO t2 VALUES(4, 'qrst')");
      stmt.execute("INSERT INTO t3 values(5, 'uvwx')");
      stmt.execute("COMMIT");
      stmt.execute("ALTER PUBLICATION pub DROP TABLE t2");
      Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(6, 'ijkl')");
      stmt.execute("INSERT INTO t2 VALUES(7, 'lmno')");
      stmt.execute("INSERT INTO t3 values(8, 'opqr')");
      stmt.execute("COMMIT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 6 from first txn, 6 from second txn, 4 from third txn.
    result.addAll(receiveMessage(stream, 16));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/5"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnValue("abcd")))));
        add(PgOutputRelationMessage.CreateForComparison("public", "t2", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("2"),
            new PgOutputMessageTupleColumnValue("defg")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/5"), LogSequenceNumber.valueOf("0/6")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 3));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("3"),
            new PgOutputMessageTupleColumnValue("mnop")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("4"),
            new PgOutputMessageTupleColumnValue("qrst")))));
        add(PgOutputRelationMessage.CreateForComparison("public", "t3", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("5"),
            new PgOutputMessageTupleColumnValue("uvwx")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/E"), 4));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("6"),
            new PgOutputMessageTupleColumnValue("ijkl")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("8"),
            new PgOutputMessageTupleColumnValue("opqr")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/E"), LogSequenceNumber.valueOf("0/F")));
      }
    };

    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void replicationConnectionConsumption() throws Exception {
    testReplicationConnectionConsumption("test_repl_slot_consumption");
  }

  @Test
  public void replicationConnectionConsumptionMultipleBatches() throws Exception {
    markClusterNeedsRecreation();
    Map<String, String> tserverFlags = super.getTServerFlags();
    // Set the batch size to a smaller value than the default of 500, so that the test is fast.
    tserverFlags.put("cdcsdk_max_consistent_records", "2");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    testReplicationConnectionConsumption("test_repl_slot_consumption_mul_batches");
  }

  @Test
  public void replicationConnectionConsumptionAllDataTypes() throws Exception {
    String create_stmt = "CREATE TABLE test_table ( "
        + "a INT PRIMARY KEY, "
        + "col_bit BIT(6), "
        + "col_boolean BOOLEAN, "
        + "col_box BOX, "
        + "col_bytea BYTEA, "
        + "col_cidr CIDR, "
        + "col_circle CIRCLE, "
        + "col_date DATE, "
        + "col_float FLOAT, "
        + "col_double DOUBLE PRECISION, "
        + "col_inet INET, "
        + "col_int INT, "
        + "col_json JSON, "
        + "col_jsonb JSONB, "
        + "col_line LINE, "
        + "col_lseg LSEG, "
        + "col_macaddr8 MACADDR8, "
        + "col_macaddr MACADDR, "
        + "col_money MONEY, "
        + "col_numeric NUMERIC, "
        + "col_path PATH, "
        + "col_point POINT, "
        + "col_polygon POLYGON, "
        + "col_text TEXT, "
        + "col_time TIME, "
        + "col_timestamp TIMESTAMP, "
        + "col_timetz TIMETZ, "
        + "col_uuid UUID, "
        + "col_varbit VARBIT(10), "
        + "col_timestamptz TIMESTAMPTZ, "
        + "col_int4range INT4RANGE, "
        + "col_int8range INT8RANGE, "
        + "col_tsrange TSRANGE, "
        + "col_tstzrange TSTZRANGE, "
        + "col_daterange DATERANGE, "
        + "col_discount coupon_discount_type)";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TYPE coupon_discount_type AS ENUM ('FIXED', 'PERCENTAGE');");
      stmt.execute(create_stmt);
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createStreamAndWaitForSnapshotTimeToPass(
        replConnection, "test_slot_repl_conn_all_data_types", "pgoutput");

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO test_table VALUES ("
          + "1, B'110110', TRUE, '((0,0),(1,1))', E'\\\\x012345', '127.0.0.1', '((0,0),1)', "
          + "'2024-02-01', 1.201, 3.14, '127.0.0.1', 42, "
          + "'{\"key\": \"value\"}', '{\"key\": \"value\"}', "
          + "'{1,2,3}', '((0,0),(1,1))', '00:11:22:33:44:55:66:77', '00:11:22:33:44:55', 100.50, "
          + "123.456, '((0,0),(1,1))', '(0,0)', '((0,0),(1,1))', 'Sample Text', '12:34:56', "
          + "'2024-02-01 12:34:56', '2024-02-01 12:34:56+00:00', "
          + "'550e8400-e29b-41d4-a716-446655440000', B'101010', '2024-02-01 12:34:56+00:00', "
          + "'[1,10)', '[100,1000)', '[2024-01-01, 2024-12-31)', "
          + "'[2024-01-01 00:00:00+00:00, 2024-12-31 15:59:59+00:00)', "
          + "'[2024-01-01, 2024-12-31)', 'FIXED');");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName("test_slot_repl_conn_all_data_types")
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 1 Relation, begin, type, insert and commit record.
    result.addAll(receiveMessage(stream, 5));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputTypeMessage.CreateForComparison("public", "coupon_discount_type"));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_table", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("col_bit", 1560),
                PgOutputRelationMessageColumn.CreateForComparison("col_boolean", 16),
                PgOutputRelationMessageColumn.CreateForComparison("col_box", 603),
                PgOutputRelationMessageColumn.CreateForComparison("col_bytea", 17),
                PgOutputRelationMessageColumn.CreateForComparison("col_cidr", 650),
                PgOutputRelationMessageColumn.CreateForComparison("col_circle", 718),
                PgOutputRelationMessageColumn.CreateForComparison("col_date", 1082),
                PgOutputRelationMessageColumn.CreateForComparison("col_float", 701),
                PgOutputRelationMessageColumn.CreateForComparison("col_double", 701),
                PgOutputRelationMessageColumn.CreateForComparison("col_inet", 869),
                PgOutputRelationMessageColumn.CreateForComparison("col_int", 23),
                PgOutputRelationMessageColumn.CreateForComparison("col_json", 114),
                PgOutputRelationMessageColumn.CreateForComparison("col_jsonb", 3802),
                PgOutputRelationMessageColumn.CreateForComparison("col_line", 628),
                PgOutputRelationMessageColumn.CreateForComparison("col_lseg", 601),
                PgOutputRelationMessageColumn.CreateForComparison("col_macaddr8", 774),
                PgOutputRelationMessageColumn.CreateForComparison("col_macaddr", 829),
                PgOutputRelationMessageColumn.CreateForComparison("col_money", 790),
                PgOutputRelationMessageColumn.CreateForComparison("col_numeric", 1700),
                PgOutputRelationMessageColumn.CreateForComparison("col_path", 602),
                PgOutputRelationMessageColumn.CreateForComparison("col_point", 600),
                PgOutputRelationMessageColumn.CreateForComparison("col_polygon", 604),
                PgOutputRelationMessageColumn.CreateForComparison("col_text", 25),
                PgOutputRelationMessageColumn.CreateForComparison("col_time", 1083),
                PgOutputRelationMessageColumn.CreateForComparison("col_timestamp", 1114),
                PgOutputRelationMessageColumn.CreateForComparison("col_timetz", 1266),
                PgOutputRelationMessageColumn.CreateForComparison("col_uuid", 2950),
                PgOutputRelationMessageColumn.CreateForComparison("col_varbit", 1562),
                PgOutputRelationMessageColumn.CreateForComparison("col_timestamptz", 1184),
                PgOutputRelationMessageColumn.CreateForComparison("col_int4range", 3904),
                PgOutputRelationMessageColumn.CreateForComparison("col_int8range", 3926),
                PgOutputRelationMessageColumn.CreateForComparison("col_tsrange", 3908),
                PgOutputRelationMessageColumn.CreateForComparison("col_tstzrange", 3910),
                PgOutputRelationMessageColumn.CreateForComparison("col_daterange", 3912),
                PgOutputRelationMessageColumn.CreateForComparison(
                    "col_discount", /* IGNORED */ 0, /* compareDataType */ false))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 36,
            Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("110110"),
                new PgOutputMessageTupleColumnValue("t"),
                new PgOutputMessageTupleColumnValue("(1,1),(0,0)"),
                new PgOutputMessageTupleColumnValue("\\x012345"),
                new PgOutputMessageTupleColumnValue("127.0.0.1/32"),
                new PgOutputMessageTupleColumnValue("<(0,0),1>"),
                new PgOutputMessageTupleColumnValue("2024-02-01"),
                new PgOutputMessageTupleColumnValue("1.20100000000000007"),
                new PgOutputMessageTupleColumnValue("3.14000000000000012"),
                new PgOutputMessageTupleColumnValue("127.0.0.1"),
                new PgOutputMessageTupleColumnValue("42"),
                new PgOutputMessageTupleColumnValue("{\"key\": \"value\"}"),
                new PgOutputMessageTupleColumnValue("{\"key\": \"value\"}"),
                new PgOutputMessageTupleColumnValue("{1,2,3}"),
                new PgOutputMessageTupleColumnValue("[(0,0),(1,1)]"),
                new PgOutputMessageTupleColumnValue("00:11:22:33:44:55:66:77"),
                new PgOutputMessageTupleColumnValue("00:11:22:33:44:55"),
                new PgOutputMessageTupleColumnValue("$100.50"),
                new PgOutputMessageTupleColumnValue("123.456"),
                new PgOutputMessageTupleColumnValue("((0,0),(1,1))"),
                new PgOutputMessageTupleColumnValue("(0,0)"),
                new PgOutputMessageTupleColumnValue("((0,0),(1,1))"),
                new PgOutputMessageTupleColumnValue("Sample Text"),
                new PgOutputMessageTupleColumnValue("12:34:56"),
                new PgOutputMessageTupleColumnValue("2024-02-01 12:34:56"),
                new PgOutputMessageTupleColumnValue("12:34:56+00"),
                new PgOutputMessageTupleColumnValue("550e8400-e29b-41d4-a716-446655440000"),
                new PgOutputMessageTupleColumnValue("101010"),
                new PgOutputMessageTupleColumnValue(
                    convertTimestampToSystemTimezone("2024-02-01T12:34:56.00Z")),
                new PgOutputMessageTupleColumnValue("[1,10)"),
                new PgOutputMessageTupleColumnValue("[100,1000)"),
                new PgOutputMessageTupleColumnValue(
                    "[\"2024-01-01 00:00:00\",\"2024-12-31 00:00:00\")"),
                new PgOutputMessageTupleColumnValue(String.format("[\"%s\",\"%s\")",
                    convertTimestampToSystemTimezone("2024-01-01T00:00:00.00Z"),
                    convertTimestampToSystemTimezone("2024-12-31T15:59:59.00Z"))),
                new PgOutputMessageTupleColumnValue("[2024-01-01,2024-12-31)"),
                new PgOutputMessageTupleColumnValue("FIXED")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void consumptionOnSubsetOfColocatedTables() throws Exception {
    markClusterNeedsRecreation();
    Map<String, String> tserverFlags = super.getTServerFlags();
    // Set the batch size to a smaller value than the default of 500, so that the
    // test is fast.
    tserverFlags.put("cdcsdk_max_consistent_records", "2");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);
    String slotName = "test_slot";

    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("CREATE DATABASE col_db WITH colocation = true");
    }

    Connection conn = getConnectionBuilder().withDatabase("col_db").connect();
    try (Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE TABLE t1 (id INT PRIMARY KEY, name TEXT) WITH (COLOCATED = true);");
      stmt.execute("CREATE TABLE t2 (id INT PRIMARY KEY, name TEXT) WITH (COLOCATED = true);");
      stmt.execute("CREATE TABLE t3 (id INT PRIMARY KEY, name TEXT) WITH (COLOCATED = true);");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1, t2");
      // Close statement.
      stmt.close();
    }
    conn.close();

    Connection conn2 =
        getConnectionBuilder().withDatabase("col_db").withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn2.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = conn2.createStatement()) {
      stmt.execute("INSERT INTO t1 VALUES(1, 'abc')");
      stmt.execute("INSERT INTO t2 VALUES(2, 'def')");
      stmt.execute("INSERT INTO t3 VALUES(3, 'hij')");
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(4, 'klm')");
      stmt.execute("INSERT INTO t2 VALUES(5, 'nop')");
      stmt.execute("INSERT INTO t3 VALUES(6, 'qrs')");
      stmt.execute("COMMIT");
      stmt.close();
    }

    PGReplicationStream stream = replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 2 Relation (t1 & t2) + 3 records/txn (B+I+C) * 2 txns (performed on t1 & t2
    // respectively) + 2 records/txn (B+C) * 1 txn (performed on t3) + 4 records/txn
    // (B+I1+I2+C) * 1 multi-shard txn.
    result.addAll(receiveMessage(stream, 14));
    for (PgOutputMessage res : result) {
      LOG.info("Row = {}", res);
    }

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("id", 23),
                PgOutputRelationMessageColumn.CreateForComparison("name", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abc")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/7"), 3));
        add(PgOutputRelationMessage.CreateForComparison("public", "t2", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("id", 23),
                PgOutputRelationMessageColumn.CreateForComparison("name", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("def")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/7"), LogSequenceNumber.valueOf("0/8")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 4));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/F"), 5));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("4"),
                new PgOutputMessageTupleColumnValue("klm")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("5"),
                new PgOutputMessageTupleColumnValue("nop")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/F"), LogSequenceNumber.valueOf("0/10")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
    conn2.close();
  }

  @Test
  public void replicationConnectionConsumptionDisabled() throws Exception {
    markClusterNeedsRecreation();
    Map<String, String> tserverFlags = super.getTServerFlags();
    tserverFlags.put("ysql_TEST_enable_replication_slot_consumption", "false");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    replConnection.createReplicationSlot()
        .logical()
        .withSlotName("test_slot_repl_conn_disabled")
        .withOutputPlugin("pgoutput")
        .make();

    String expectedErrorMessage = "ERROR: StartReplication is unavailable";

    boolean exceptionThrown = false;
    try {
      replConnection.replicationStream()
          .logical()
          .withSlotName("test_slot_repl_conn_disabled")
          .withStartPosition(LogSequenceNumber.valueOf(0L))
          .withSlotOption("proto_version", 1)
          .withSlotOption("publication_names", "pub")
          .start();
    } catch (PSQLException e) {
      exceptionThrown = true;
      if (StringUtils.containsIgnoreCase(e.getMessage(), expectedErrorMessage)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
            e.getMessage(), expectedErrorMessage));
      }
    }

    assertTrue("Expected an exception but wasn't thrown", exceptionThrown);
  }

  @Test
  public void replicationConnectionConsumptionAttributeDroppedRecreated() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");

      stmt.execute("ALTER TABLE t1 DROP COLUMN b");
      stmt.execute("ALTER TABLE t1 ADD COLUMN b int");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createStreamAndWaitForSnapshotTimeToPass(
        replConnection, "test_slot_repl_conn_attribute_dropped", "pgoutput");

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO t1 VALUES(1, 1)");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName("test_slot_repl_conn_attribute_dropped")
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 1 Relation, begin, insert and commit record.
    result.addAll(receiveMessage(stream, 4));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("1")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testInnerLSNValues() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    String slotName = "test_inner_lsn_values";
    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd')");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    // The LSN of the BEGIN record is the LSN value of the first operation within the transaction.
    receiveMessage(stream, 1);
    assertEquals(LogSequenceNumber.valueOf(3L), stream.getLastReceiveLSN());

    // RELATION records don't have an LSN.
    receiveMessage(stream, 1);
    assertEquals(LogSequenceNumber.valueOf(0L), stream.getLastReceiveLSN());

    // INSERT record.
    receiveMessage(stream, 1);
    assertEquals(LogSequenceNumber.valueOf(3L), stream.getLastReceiveLSN());

    // The LSN of the COMMIT record is the end_lsn i.e. commit_lsn + 1. This points to the next
    // record after the transaction.
    receiveMessage(stream, 1);
    assertEquals(LogSequenceNumber.valueOf(5L), stream.getLastReceiveLSN());

    stream.close();
  }

  private LogSequenceNumber getRestartLSN(Connection connection, String slotName) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ResultSet res = stmt.executeQuery(String.format(
          "select restart_lsn from pg_replication_slots where slot_name = '%s'", slotName));
      assertTrue(res.next());
      String value = res.getString("restart_lsn");
      return LogSequenceNumber.valueOf(value);
    }
  }

  private void waitForRestartLSN(Connection connection, String slotName, long expectedLSN)
      throws Exception {
    LOG.info("Waiting for restart LSN to become {}", expectedLSN);
    TestUtils.waitFor(() -> {
      LogSequenceNumber restartLSN = getRestartLSN(connection, slotName);
      LOG.info("The actual restartLSN {}", restartLSN);
      return restartLSN.asLong() == expectedLSN;
    }, 10000);
    LOG.info("Done waiting for restart LSN to become {}", expectedLSN);
  }

  @Test
  public void testReplicationConnectionUpdateRestartLSN() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
      stmt.execute("CREATE TABLE test (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "test_update_restart_lsn";
    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO test VALUES(2, 'defg')");
      stmt.execute("COMMIT");

      stmt.execute("INSERT INTO test VALUES(3, 'xyz')");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(4, 'pqr')");
      stmt.execute("INSERT INTO test VALUES(5, 'ijk')");
      stmt.execute("COMMIT");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(6, 'lmn')");
      stmt.execute("INSERT INTO test VALUES(7, 'opq')");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // Get the first 3 messages (BEGIN, RELATION, 1st INSERT).
    result.addAll(receiveMessage(stream, 3));

    List<Long> expectedRestartLSNs = new ArrayList<Long>() {
      {
        add(1L); // snapshot lsn, streaming starts from 2.
        add(5L); // commit_lsn of transaction 1.
        add(8L); // commit_lsn of transaction 2.
        add(12L); // commit_lsn of transaction 3.
        add(16L); // commit_lsn of transaction 4.
      }
    };

    // The restart LSN is 1 because:
    // 1. We haven't consumed the whole of the first transaction.
    // 2. There is no previous transaction which has been fully consumed.
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, expectedRestartLSNs.get(0));

    // Get the 2nd INSERT message.
    result.addAll(receiveMessage(stream, 1));

    // The restart LSN is 1 because:
    // 1. We haven't consumed the whole of the first transaction (commit is pending).
    // 2. There is no previous transaction which has been fully consumed.
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, expectedRestartLSNs.get(0));

    // Get the COMMIT message.
    result.addAll(receiveMessage(stream, 1));

    // Now that we have consumed the complete transaction 1, the restart_lsn should be the
    // commit_lsn of the commit record which is 5 (LSN values start from 2 and there are 4 records
    // (begin, 2 inserts and commit)).
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, expectedRestartLSNs.get(1));

    // Read the second transaction completely (BEGIN, INSERT, COMMIT) and now the restart_lsn should
    // be 8 which is the commit_lsn of the second transaction.
    result.addAll(receiveMessage(stream, 3));
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, expectedRestartLSNs.get(2));

    // Receive the next transactions completely and the next one after that partially (one insert
    // out of the two).
    result.addAll(receiveMessage(stream, 6));

    // The restart_lsn value should be the commit_lsn of the fully flushed transaction i.e. 12.
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, expectedRestartLSNs.get(3));

    // Receive the two remaining records of the last transaction (insert and commit). Post that, the
    // restart_lsn should be the commit_lsn of the last transaction i.e. 16.
    result.addAll(receiveMessage(stream, 2));
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, expectedRestartLSNs.get(4));

    stream.close();
  }

  @Test
  public void testReplicationConnectionUpdateRestartLSNWithRestarts() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
      stmt.execute("CREATE TABLE test (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "test_update_restart_lsn_with_restarts";
    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO test VALUES(2, 'defg')");
      stmt.execute("COMMIT");

      stmt.execute("INSERT INTO test VALUES(3, 'xyz')");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(4, 'pqr')");
      stmt.execute("INSERT INTO test VALUES(5, 'ijk')");
      stmt.execute("COMMIT");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(6, 'lmn')");
      stmt.execute("INSERT INTO test VALUES(7, 'opq')");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    // Consume the following changes:
    // 1. Transaction 1 completely: 5 records (BEGIN, RELATION, INSERT, INSERT, COMMIT)
    // 2. Transaction 2 completely: 3 records (BEGIN, INSERT, COMMIT)
    // 3. Transaction 3 partially: 2 records (BEGIN, INSERT). 2 more records pending.
    receiveMessage(stream, 10);

    // Set the confirmed_flush to 7 which is the LSN of the INSERT record of transaction 2 above.
    // Since the restart_lsn is set to the commit_lsn of the last fully flushed transaction, the
    // restart_lsn is set to 5 which is the commit_lsn of transaction 1.
    stream.setFlushedLSN(LogSequenceNumber.valueOf(7L));
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, 5L);

    // Close this stream and the connection.
    stream.close();
    conn.close();

    Connection conn2 =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection2 = conn2.unwrap(PGConnection.class).getReplicationAPI();

    stream = replConnection2.replicationStream()
                 .logical()
                 .withSlotName(slotName)
                 // The start position will be fetched from the restart_lsn persisted.
                 .withStartPosition(LogSequenceNumber.valueOf(0L))
                 .withSlotOption("proto_version", 1)
                 .withSlotOption("publication_names", "pub")
                 .start();

    // Streaming should start from transaction 2 onwards as the restart_lsn is 5.
    // Transaction 2 - 4 records (BEGIN, RELATION, INSERT, COMMIT)
    // Transaction 3 - 4 records (BEGIN, INSERT, INSERT, COMMIT)
    // Transaction 4 - 4 records (BEGIN, INSERT, INSERT, COMMIT)
    //
    // Note that RELATION gets sent again after restart.
    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream, 12));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/8"), 3));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("xyz")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/8"), LogSequenceNumber.valueOf("0/9")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/C"), 4));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("4"),
                new PgOutputMessageTupleColumnValue("pqr")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("5"),
                new PgOutputMessageTupleColumnValue("ijk")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/C"), LogSequenceNumber.valueOf("0/D")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/10"), 5));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("6"),
                new PgOutputMessageTupleColumnValue("lmn")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("7"),
                new PgOutputMessageTupleColumnValue("opq")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/10"), LogSequenceNumber.valueOf("0/11")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testStartLsnValues() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
      stmt.execute("CREATE TABLE test (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "test_start_lsn_values";
    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO test VALUES(2, 'defg')");
      stmt.execute("COMMIT");

      stmt.execute("INSERT INTO test VALUES(3, 'xyz')");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(4, 'pqr')");
      stmt.execute("INSERT INTO test VALUES(5, 'ijk')");
      stmt.execute("COMMIT");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(6, 'lmn')");
      stmt.execute("INSERT INTO test VALUES(7, 'opq')");
      stmt.execute("COMMIT");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(8, 'rst')");
      stmt.execute("INSERT INTO test VALUES(9, 'uvw')");
      stmt.execute("COMMIT");
    }

    // Start streaming with LSN "0/3" which is midway in the first transaction. So we should still
    // receive all the contents of the first transaction.
    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(3L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    // Consume transaction 1 completely: 5 records (BEGIN, RELATION, INSERT, INSERT, COMMIT)
    List<PgOutputMessage> firstPoll = new ArrayList<PgOutputMessage>();
    firstPoll.addAll(receiveMessage(stream, 5));

    stream.close();
    conn.close();

    Connection conn2 =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection2 = conn2.unwrap(PGConnection.class).getReplicationAPI();

    stream = replConnection2.replicationStream()
                 .logical()
                 .withSlotName(slotName)
                 // Ask for streaming to start from "0/7" which is in the middle of transaction 2,
                 // so we should start receiving from the start of the transaction 2.
                 .withStartPosition(LogSequenceNumber.valueOf(7L))
                 .withSlotOption("proto_version", 1)
                 .withSlotOption("publication_names", "pub")
                 .start();

    // Streaming should start from transaction 2 onwards as we have asked to receive from lsn 7
    // onwards.
    //
    // Transaction 2 - 4 records (BEGIN, RELATION, INSERT, COMMIT)
    // Transaction 3 - 4 records (BEGIN, INSERT, INSERT, COMMIT)
    //
    // Note that RELATION gets sent again after restart.
    List<PgOutputMessage> secondPoll = new ArrayList<PgOutputMessage>();
    secondPoll.addAll(receiveMessage(stream, 8));

    stream.close();
    conn2.close();

    Connection conn3 =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection3 = conn3.unwrap(PGConnection.class).getReplicationAPI();

    stream = replConnection3.replicationStream()
                 .logical()
                 .withSlotName(slotName)
                 // Ask for streaming to start from "13L" which is at the start of the txn 4.
                 .withStartPosition(LogSequenceNumber.valueOf(13L))
                 .withSlotOption("proto_version", 1)
                 .withSlotOption("publication_names", "pub")
                 .start();

    // Transaction 4 - 5 records (BEGIN, RELATION, INSERT, INSERT, COMMIT)
    List<PgOutputMessage> thirdPoll = new ArrayList<PgOutputMessage>();
    thirdPoll.addAll(receiveMessage(stream, 5));

    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    stream.close();

    Connection conn4 =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection4 = conn4.unwrap(PGConnection.class).getReplicationAPI();

    stream = replConnection4.replicationStream()
                 .logical()
                 .withSlotName(slotName)
                 // Ask for streaming to start from "0L" to replicate scenario where start_lsn <
                 // confirmed_flush. The confirmed_flush takes precedence in these cases.
                 .withStartPosition(LogSequenceNumber.valueOf(0L))
                 .withSlotOption("proto_version", 1)
                 .withSlotOption("publication_names", "pub")
                 .start();

    // Transaction 5 - 5 records (BEGIN, RELATION, INSERT, INSERT, COMMIT)
    // Everything has been confirmed to be flushed.
    List<PgOutputMessage> fourthPoll = new ArrayList<PgOutputMessage>();
    fourthPoll.addAll(receiveMessage(stream, 5));

    List<PgOutputMessage> expectedFirstPoll = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/5"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("defg")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/5"), LogSequenceNumber.valueOf("0/6")));
      }
    };
    assertEquals(expectedFirstPoll, firstPoll);

    List<PgOutputMessage> expectedSecondPoll = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/8"), 3));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("xyz")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/8"), LogSequenceNumber.valueOf("0/9")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/C"), 4));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("4"),
                new PgOutputMessageTupleColumnValue("pqr")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("5"),
                new PgOutputMessageTupleColumnValue("ijk")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/C"), LogSequenceNumber.valueOf("0/D")));
      }
    };
    assertEquals(expectedSecondPoll, secondPoll);

    List<PgOutputMessage> expectedThirdPoll = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/10"), 5));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("6"),
                new PgOutputMessageTupleColumnValue("lmn")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("7"),
                new PgOutputMessageTupleColumnValue("opq")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/10"), LogSequenceNumber.valueOf("0/11")));
      }
    };
    assertEquals(expectedThirdPoll, thirdPoll);

    List<PgOutputMessage> expectedFourthPoll = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/14"), 6));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("8"),
                new PgOutputMessageTupleColumnValue("rst")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("9"),
                new PgOutputMessageTupleColumnValue("uvw")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/14"), LogSequenceNumber.valueOf("0/15")));
      }
    };
    assertEquals(expectedFourthPoll, fourthPoll);

    stream.close();
    conn3.close();
  }

  @Test
  public void testWalsenderGracefulShutdownWithCDCServiceError() throws Exception {
    markClusterNeedsRecreation();
    Map<String, String> tserverFlags = super.getTServerFlags();
    tserverFlags.put("TEST_cdc_force_destroy_virtual_wal_failure", "true");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    String slotName = "test_repl_slot_graceful_shutdown";
    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO test VALUES(1, 'xyz')");
    }

    PGReplicationStream stream = replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .start();

    // BEGIN, RELATION, INSERT, COMMIT.
    receiveMessage(stream, 4);

    // This should trigger shutdown which will trigger Walsender to destroy the virtual wal where we
    // have forced a failure.
    stream.close();
  }

  @Test
  public void testWithDDLs() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
      stmt.execute("CREATE TABLE test (a int primary key, b text)");
      stmt.execute("CREATE TABLE test_2 (a int primary key, b bool)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "test_with_ddls";
    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO test VALUES(2, 'defg')");
      stmt.execute("INSERT INTO test_2 VALUES(1, 't')");
      stmt.execute("COMMIT");

      stmt.execute("ALTER TABLE test DROP COLUMN b");
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(4)");
      stmt.execute("INSERT INTO test_2 VALUES(100, 'f')");
      stmt.execute("COMMIT");

      stmt.execute("ALTER TABLE test_2 DROP COLUMN b");
      stmt.execute("INSERT INTO test_2 VALUES(2)");

      stmt.execute("ALTER TABLE test ADD COLUMN b int");
      stmt.execute("INSERT INTO test VALUES(5, 5)");
      stmt.execute("INSERT INTO test VALUES(6, 6)");

      stmt.execute("ALTER TABLE test ADD COLUMN c int");
      stmt.execute("ALTER TABLE test ADD COLUMN d int");
      stmt.execute("ALTER TABLE test_2 ADD COLUMN c int");

      stmt.execute("INSERT INTO test_2 VALUES(3, 3)");
      stmt.execute("INSERT INTO test VALUES(7, 7, 7, 7)");
      stmt.execute("INSERT INTO test VALUES(8, 8, 8, 8)");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();

    result.addAll(receiveMessage(stream, 34));


    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        // In the first transaction that gets streamed, we got RELATION messages for both since they
        // are being streamed for the first time.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/6"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("defg")))));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_2", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 16))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("t")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/6"), LogSequenceNumber.valueOf("0/7")));

        // We get the RELATION for test because a DDL happened on it. We don't get it for test_2
        // because of no DDL.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 3));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 1,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("4")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("100"),
                new PgOutputMessageTupleColumnValue("f")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));

        // We get a relation message of test_2 due to the DDL (DROP COLUMN b)
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/D"), 4));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_2", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 1,
            Arrays.asList(new PgOutputMessageTupleColumnValue("2")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/D"), LogSequenceNumber.valueOf("0/E")));

        // We get a relation message of test due to the DDL (ADD COLUMN b int)
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/10"), 5));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(
                PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("5"),
                new PgOutputMessageTupleColumnValue("5")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/10"), LogSequenceNumber.valueOf("0/11")));

        // No DDLs done, so no relation messages expected.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/13"), 6));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("6"),
                new PgOutputMessageTupleColumnValue("6")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/13"), LogSequenceNumber.valueOf("0/14")));

        // We did two DDLs on test and one DDL on test_2. We get the RELATION message of test_2 here
        // since the transaction only touches test_2.
        // The RELATION message for test will be sent with the first DML on it which is the next
        // transaction.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/16"), 7));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_2", 'c',
            Arrays.asList(
                PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("c", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("3")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/16"), LogSequenceNumber.valueOf("0/17")));

        // Transaction that has the first DML after the two DDL operations on table 'test'.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/19"), 8));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(
                PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 23),
                PgOutputRelationMessageColumn.CreateForComparison("c", 23),
                PgOutputRelationMessageColumn.CreateForComparison("d", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 4,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("7"),
                new PgOutputMessageTupleColumnValue("7"),
                new PgOutputMessageTupleColumnValue("7"),
                new PgOutputMessageTupleColumnValue("7")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/19"), LogSequenceNumber.valueOf("0/1A")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/1C"), 9));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 4,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("8"),
                new PgOutputMessageTupleColumnValue("8"),
                new PgOutputMessageTupleColumnValue("8"),
                new PgOutputMessageTupleColumnValue("8")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/1C"), LogSequenceNumber.valueOf("0/1D")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  // The reorderbuffer spills transactions with more than max_changes_in_memory (4096) changes
  // on the disk. This test asserts that such transactions also work correctly.
  @Test
  public void testReplicationWithSpilledTransaction() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text, c bool)");
      // CHANGE is the default replica identity but we explicitly set it here so that don't need to
      // update this test in case we change the default.
      stmt.execute("ALTER TABLE t1 REPLICA IDENTITY CHANGE");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }
    String slotName = "test_with_spilled_txn";
    // This must be more than max_changes_in_memory defined in reorderbuffer.c
    int numInserts = 5000;

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      for (int i = 0; i < numInserts; i++) {
        stmt.execute(
            String.format("INSERT INTO t1 VALUES(%s, '%s', true)", i, String.format("text_%d", i)));
      }
      stmt.execute("UPDATE t1 SET b = 'UPDATED_text_1' WHERE a = 1");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 1 Relation, 1 begin, 5000 insert, 1 update, 1 commit.
    result.addAll(receiveMessage(stream, 5004));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        // Note: 0x138C = 5004 in decimal which is the lsn of the commit record as expected.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/138C"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c' /* replicaIdentity */,
            Arrays.asList(
                PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25),
                PgOutputRelationMessageColumn.CreateForComparison("c", 16))));
      }
    };
    for (int i = 0; i < numInserts; i++) {
      expectedResult.add(
          PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                  new PgOutputMessageTupleColumnValue(String.format("%d", i)),
                  new PgOutputMessageTupleColumnValue(String.format("text_%d", i)),
                  new PgOutputMessageTupleColumnValue("t")))));
    }
    expectedResult.add(PgOutputUpdateMessage.CreateForComparison(
        new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnToasted(),
                new PgOutputMessageTupleColumnToasted(),
                new PgOutputMessageTupleColumnToasted())),
        new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("UPDATED_text_1"),
                new PgOutputMessageTupleColumnToasted()))));
    expectedResult.add(PgOutputCommitMessage.CreateForComparison(
        LogSequenceNumber.valueOf("0/138C"), LogSequenceNumber.valueOf("0/138D")));

    assertEquals(expectedResult, result);
    stream.close();
  }

  private void testReplicationWithSpilledTransactionAndRestart(boolean differentNode)
      throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }
    String slotName = "test_with_spilled_txn_restart_different_node";
    // This must be more than max_changes_in_memory defined in reorderbuffer.c
    int numInserts = 5000;

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO t1 VALUES(999999, '999999')");

      stmt.execute("BEGIN");
      for (int i = 0; i < numInserts; i++) {
        stmt.execute(
            String.format("INSERT INTO t1 VALUES(%s, '%s')", i, String.format("text_%d", i)));
      }
      stmt.execute("UPDATE t1 SET b = 'UPDATED_text_1' WHERE a = 1");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // Txn1: 1 RELATION, 1 BEGIN, 1 INSERT, 1 COMMIT
    result.addAll(receiveMessage(stream, 4));

    // Txn2 (partially): 1 BEGIN, 2800 INSERT
    receiveMessage(stream, 2801);

    // Ack Txn1.
    // Note that getLastReceiveLSN() will return the LSN of the last record we read using
    // receiveMessage above which will be 2806.
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, 4L);
    stream.close();
    conn.close();

    if (differentNode) {
      // Connect with the other tserver.
      conn = getConnectionBuilder().withTServer(1).replicationConnect();
    } else {
      conn = getConnectionBuilder().withTServer(0).replicationConnect();
    }
    replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    stream = replConnection.replicationStream()
                 .logical()
                 .withSlotName(slotName)
                 // Streaming should begin after Txn1.
                 .withStartPosition(LogSequenceNumber.valueOf(0L))
                 .withSlotOption("proto_version", 1)
                 .withSlotOption("publication_names", "pub")
                 .start();

    // Txn2: 1 BEGIN, 1 RELATION, 5000 INSERT, 1 UPDATE, 1 COMMIT
    // The whole txn2 will be streamed again since we never received it fully.
    result.addAll(receiveMessage(stream, 5004));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c' /* replicaIdentity */,
            Arrays.asList(
                PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("999999"),
                new PgOutputMessageTupleColumnValue("999999")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/138F"), 3));
        // Relation message gets sent again due to the restart.
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c' /* replicaIdentity */,
            Arrays.asList(
                PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
      }
    };
    for (int i = 0; i < numInserts; i++) {
      expectedResult.add(
          PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
              Arrays.asList(
                  new PgOutputMessageTupleColumnValue(String.format("%d", i)),
                  new PgOutputMessageTupleColumnValue(String.format("text_%d", i))))));
    }
    expectedResult.add(PgOutputUpdateMessage.CreateForComparison(
        new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnToasted(),
                new PgOutputMessageTupleColumnToasted())),
        new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("UPDATED_text_1")))));
    expectedResult.add(PgOutputCommitMessage.CreateForComparison(
        LogSequenceNumber.valueOf("0/138F"), LogSequenceNumber.valueOf("0/1390")));

    assertEquals(expectedResult, result);
    stream.close();
    conn.close();
  }

  @Test
  public void testReplicationWithSpilledTransactionAndRestartOnSameNode() throws Exception {
    testReplicationWithSpilledTransactionAndRestart(false /* differentNode */);
  }

  @Test
  public void testReplicationWithSpilledTransactionAndRestartOnDifferentNode() throws Exception {
    testReplicationWithSpilledTransactionAndRestart(true /* differentNode */);
  }

  // The aim is to assert that the replica identity which is sent in the RELATION message is the
  // value which existed at the time of stream creation. Future ALTER TABLE REPLICA IDENTITY
  // statement should have an impact.
  @Test
  public void testWithAlterReplicaIdentity() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t2 (a int primary key, b text)");

      // CHANGE is the default but we do it explicitly so that the tests do not need changing if we
      // change the default.
      stmt.execute("ALTER TABLE t1 REPLICA IDENTITY CHANGE");
      stmt.execute("ALTER TABLE t2 REPLICA IDENTITY FULL");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "testWithAlterReplicaIdentity";
    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "pgoutput");
    try (Statement stmt = connection.createStatement()) {
      // After the stream creation, we are changing the replica identity of each table.
      stmt.execute("ALTER TABLE t1 REPLICA IDENTITY NOTHING");
      stmt.execute("ALTER TABLE t2 REPLICA IDENTITY CHANGE");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO t2 VALUES(2, 'defg')");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();

    result.addAll(receiveMessage(stream, 6));


    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/5"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c' /* replicaIdentity */,
            Arrays.asList(
                PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd")))));
        add(PgOutputRelationMessage.CreateForComparison("public", "t2", 'f' /* replicaIdentity */,
            Arrays.asList(
                PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("defg")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/5"), LogSequenceNumber.valueOf("0/6")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testWithTestDecodingPlugin() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("DROP TABLE IF EXISTS t3");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text, c bool)");
      stmt.execute("CREATE TABLE t2 (a int primary key, b text, c bool)");
      stmt.execute("CREATE TABLE t3 (a int primary key, b text, c bool)");

      // CHANGE is the default but we do it explicitly so that the tests do not need changing if we
      // change the default.
      stmt.execute("ALTER TABLE t1 REPLICA IDENTITY CHANGE");
      stmt.execute("ALTER TABLE t2 REPLICA IDENTITY FULL");
      stmt.execute("ALTER TABLE t3 REPLICA IDENTITY DEFAULT");
    }

    String slotName = "test_with_test_decoding";
    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName, "test_decoding");
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd', true)");
      stmt.execute("INSERT INTO t1 VALUES(2, 'defg', true)");
      stmt.execute("INSERT INTO t1 VALUES(3, 'hijk', false)");
      stmt.execute("UPDATE t1 SET b = 'updated_abcd' WHERE a = 1");
      stmt.execute("UPDATE t1 SET b = NULL, c = false WHERE a = 2");
      stmt.execute("DELETE FROM t1 WHERE a = 2");

      stmt.execute("INSERT INTO t2 VALUES(1, 'abcd', true)");
      stmt.execute("UPDATE t2 SET b = 'updated_abcd' WHERE a = 1");
      stmt.execute("DELETE FROM t2 WHERE a = 1");

      stmt.execute("INSERT INTO t3 VALUES(1, 'abcd', true)");
      stmt.execute("UPDATE t3 SET b = 'updated_abcd' WHERE a = 1");
      stmt.execute("DELETE FROM t3 WHERE a = 1");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("include-xids", true)
                                     .start();

    List<String> result = new ArrayList<String>();
    result.addAll(receiveTestDecodingMessages(stream, 36));

    List<String> expectedResult = new ArrayList<String>() {
      {
        add("BEGIN 2");
        add("table public.t1: INSERT: a[integer]:1 b[text]:'abcd' c[boolean]:true");
        add("COMMIT 2");

        add("BEGIN 3");
        add("table public.t1: INSERT: a[integer]:2 b[text]:'defg' c[boolean]:true");
        add("COMMIT 3");

        add("BEGIN 4");
        add("table public.t1: INSERT: a[integer]:3 b[text]:'hijk' c[boolean]:false");
        add("COMMIT 4");

        add("BEGIN 5");
        add("table public.t1: UPDATE: old-key: new-tuple: a[integer]:1 b[text]:'updated_abcd'" +
                " c[boolean]:unchanged-toast-datum");
        add("COMMIT 5");

        add("BEGIN 6");
        add("table public.t1: UPDATE: old-key: new-tuple: a[integer]:2 b[text]:null " +
                "c[boolean]:false");
        add("COMMIT 6");

        add("BEGIN 7");
        add("table public.t1: DELETE: a[integer]:2");
        add("COMMIT 7");

        add("BEGIN 8");
        add("table public.t2: INSERT: a[integer]:1 b[text]:'abcd' c[boolean]:true");
        add("COMMIT 8");

        add("BEGIN 9");
        add("table public.t2: UPDATE: old-key: a[integer]:1 b[text]:'abcd' c[boolean]:true " +
                "new-tuple: a[integer]:1 b[text]:'updated_abcd' c[boolean]:true");
        add("COMMIT 9");

        add("BEGIN 10");
        add("table public.t2: DELETE: a[integer]:1 b[text]:'updated_abcd' c[boolean]:true");
        add("COMMIT 10");

        add("BEGIN 11");
        add("table public.t3: INSERT: a[integer]:1 b[text]:'abcd' c[boolean]:true");
        add("COMMIT 11");

        add("BEGIN 12");
        add("table public.t3: UPDATE: old-key: new-tuple: a[integer]:1 b[text]:'updated_abcd' " +
                "c[boolean]:true");
        add("COMMIT 12");

        add("BEGIN 13");
        add("table public.t3: DELETE: a[integer]:1");
        add("COMMIT 13");
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }
}
