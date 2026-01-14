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
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.yugabyte.PGConnection;
import com.yugabyte.replication.LogSequenceNumber;
import com.yugabyte.replication.PGReplicationConnection;
import com.yugabyte.replication.PGReplicationStream;
import com.yugabyte.util.PSQLException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
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
import java.util.Set;
import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.pgsql.PgOutputMessageDecoder.*;
import org.yb.util.BuildTypeUtil;

@RunWith(value = YBTestRunner.class)
public class TestPgReplicationSlot extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgReplicationSlot.class);

  private static int kMultiplier = BuildTypeUtil.nonSanitizerVsSanitizer(1, 3);
  private static int kPublicationRefreshIntervalSec = 5;

  private static final String YB_OUTPUT_PLUGIN_NAME = "yboutput";

  private static final String PG_OUTPUT_PLUGIN_NAME = "pgoutput";

  private static final String HASH_RANGE_SLOT_OPTION = "hash_range";

  private static final String WAL2JSON_PLUGIN_NAME = "wal2json";

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
        "cdcsdk_publication_list_refresh_interval_secs","" + kPublicationRefreshIntervalSec);
    flagMap.put("cdc_send_null_before_image_if_not_exists", "true");
    flagMap.put("TEST_dcheck_for_missing_schema_packing", "false");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put(
      "vmodule", "cdc_service=4,cdcsdk_producer=4");
    return flagMap;
  }

  void createSlot(PGReplicationConnection replConnection, String slotName, String pluginName)
      throws Exception {
    replConnection.createReplicationSlot()
        .logical()
        .withSlotName(slotName)
        .withOutputPlugin(pluginName)
        .make();
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
    String[] wal_levels = {"minimal", "replica", "logical"};
    for (String wal_level : wal_levels) {
      LOG.info("Testing replicationConnectionCreateDrop with wal_level = {}", wal_level);
      Map<String, String> tserverFlags = super.getTServerFlags();
      tserverFlags.put("ysql_pg_conf", String.format("wal_level=%s", wal_level));
      restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

      Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
      PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

      replConnection.createReplicationSlot()
          .logical()
          .withSlotName("test_slot_repl_conn")
          .withOutputPlugin(YB_OUTPUT_PLUGIN_NAME)
          .make();
      replConnection.dropReplicationSlot("test_slot_repl_conn");
    }
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
          .withOutputPlugin(YB_OUTPUT_PLUGIN_NAME)
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

  private List<PgOutputMessage> CreateMessages(PgOutputMessage... messages) {
    return Arrays.asList(messages);
  }

  private static String toString(ByteBuffer buffer) {
    int offset = buffer.arrayOffset();
    byte[] source = buffer.array();
    int length = source.length - offset;

    return new String(source, offset, length);
  }

  private List<String> receiveStringMessages(PGReplicationStream stream, int count)
      throws Exception {
    List<String> result = new ArrayList<String>(count);
    for (int index = 0; index < count; index++) {
      String message = toString(stream.read());
      result.add(message);
      LOG.info("Row = {}", message);
    }

    return result;
  }

  private void validateChange(JsonObject jsonObject, int origin_id, int count) {
    assertEquals(jsonObject.get("origin").getAsInt(), origin_id);
    assertEquals(jsonObject.getAsJsonArray("change").size(), count);
  }

  // TODO(#20726): Add more test cases covering:
  // 1. INSERTs in a BEGIN/COMMIT block
  // 2. Single shard transactions
  // 3. Transactions with savepoints (commit/abort subtxns)
  // 4. Transactions after table rewrite operations like ADD PRIMARY KEY
  // 5. Add a table with REPLICA IDENTITY NOTHING.

  void testReplicationConnectionConsumption(String slotName, String pluginName) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("DROP TABLE IF EXISTS t3");
      if (pluginName.equals(YB_OUTPUT_PLUGIN_NAME)) {
        stmt.execute("CREATE TABLE t1 (a int primary key, b text, c bool)");
        // CHANGE is the default but we do it explicitly so that the tests do not need changing if
        // we change the default
        stmt.execute("ALTER TABLE t1 REPLICA IDENTITY CHANGE");
      }
      stmt.execute("CREATE TABLE t2 (a int primary key, b text, c bool)");
      stmt.execute("CREATE TABLE t3 (a int primary key, b text, c bool)");

      stmt.execute("ALTER TABLE t2 REPLICA IDENTITY DEFAULT");
      stmt.execute("ALTER TABLE t3 REPLICA IDENTITY FULL");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, pluginName);
    try (Statement stmt = connection.createStatement()) {
      // Do more than 2 DMLs, since replicationConnectionConsumptionMultipleBatches tests the
      // case when #records > cdcsdk_max_consistent_records.
      if (pluginName.equals(YB_OUTPUT_PLUGIN_NAME)) {
        stmt.execute("INSERT INTO t1 VALUES(1, 'abcd', true)");
        stmt.execute("INSERT INTO t1 VALUES(2, 'defg', true)");
        stmt.execute("INSERT INTO t1 VALUES(3, 'hijk', false)");
      }
      stmt.execute("INSERT INTO t2 VALUES(1, 'abcd', true)");
      stmt.execute("INSERT INTO t2 VALUES(2, 'defg', true)");
      stmt.execute("INSERT INTO t3 VALUES(1, 'abcd', true)");
      stmt.execute("INSERT INTO t3 VALUES(2, 'defg', true)");

      if (pluginName.equals(YB_OUTPUT_PLUGIN_NAME)) {
        stmt.execute("UPDATE t1 SET b = 'updated_abcd' WHERE a = 1");
        stmt.execute("UPDATE t1 SET b = NULL, c = false WHERE a = 2");
      }
      stmt.execute("UPDATE t2 SET b = 'updated_abcd' WHERE a = 1");
      stmt.execute("UPDATE t2 SET b = NULL, c = false WHERE a = 2");
      stmt.execute("UPDATE t3 SET b = 'updated_abcd' WHERE a = 1");
      stmt.execute("UPDATE t3 SET b = NULL, c = false WHERE a = 2");

      if (pluginName.equals(YB_OUTPUT_PLUGIN_NAME)) {
        stmt.execute("DELETE FROM t1 WHERE a = 2");
      }

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t2 VALUES (3, 'ghij', true)");
      stmt.execute("UPDATE t2 SET b = 'updated_ghij', c = false WHERE a = 3");
      stmt.execute("COMMIT");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t2 VALUES (4, 'jklm', true)");
      stmt.execute("DELETE FROM t2 WHERE a = 4");
      stmt.execute("COMMIT");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t3 VALUES (3, 'ghij', true)");
      stmt.execute("UPDATE t3 SET b = 'updated_ghij', c = false WHERE a = 3");
      stmt.execute("COMMIT");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t3 VALUES (4, 'jklm', true)");
      stmt.execute("DELETE FROM t3 WHERE a = 4");
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
    // 3 Relation, 7 * 3 (begin, insert and commit), 3 * 3 * 2 (begin, update and commit), 1 * 3
    // (begin, delete, commit).
    if (pluginName.equals(YB_OUTPUT_PLUGIN_NAME)) {
      result.addAll(receiveMessage(stream, 61));
    } else {
      result.addAll(receiveMessage(stream, 42));
    }


    // LSN Values of change records start from 2 in YSQL. LSN 1 is reserved for all snapshot
    // records.
    // Note that the LSN value passed in the BEGIN message is the commit_lsn of the
    // transaction, so it is set to "0/4" in this case as the first transaction contains the
    // following records: BEGIN(2) RELATION(NO LSN) INSERT(3) COMMIT(4).
    List<PgOutputMessage> expectedResult;
    if (pluginName.equals(YB_OUTPUT_PLUGIN_NAME)) {
      expectedResult = new ArrayList<PgOutputMessage>() {
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
            // No before image in CHANGE, so old tuple comes out as null.
            null,
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
            // No before image in CHANGE, so old tuple comes out as null.
            null,
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
            // No before image in DEFAULT, so old tuple comes out as null.
            null,
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
            // No before image in DEFAULT, so old tuple comes out as null.
            null,
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

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/2F"), 16));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("3"),
              new PgOutputMessageTupleColumnValue("ghij"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputUpdateMessage.CreateForComparison(
            // No before image in DEFAULT, so old tuple comes out as null.
            null,
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("updated_ghij"),
                new PgOutputMessageTupleColumnValue("f")))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/2F"), LogSequenceNumber.valueOf("0/30")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/33"), 17));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("4"),
              new PgOutputMessageTupleColumnValue("jklm"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("4"),
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnNull()))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/33"), LogSequenceNumber.valueOf("0/34")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/37"), 18));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("3"),
              new PgOutputMessageTupleColumnValue("ghij"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputUpdateMessage.CreateForComparison(
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                // All columns for before image in FULL come out as null.
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnNull())),
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("updated_ghij"),
                new PgOutputMessageTupleColumnValue("f")))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/37"), LogSequenceNumber.valueOf("0/38")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/3B"), 19));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("4"),
              new PgOutputMessageTupleColumnValue("jklm"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("4"),
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnNull()))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/3B"), LogSequenceNumber.valueOf("0/3C")));
        }
      };
    } else {
      expectedResult = new ArrayList<PgOutputMessage>() {
        {
          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
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
            LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/D"), 5));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("2"),
              new PgOutputMessageTupleColumnValue("defg"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/D"), LogSequenceNumber.valueOf("0/E")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/10"), 6));
          add(PgOutputUpdateMessage.CreateForComparison(
            // No before image in DEFAULT, so old tuple comes out as null.
            null,
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("updated_abcd"),
                // Even though column 'c' was not modified, all columns are sent in DEFAULT.
                new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/10"), LogSequenceNumber.valueOf("0/11")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/13"), 7));
          add(PgOutputUpdateMessage.CreateForComparison(
            // No before image in DEFAULT, so old tuple comes out as null.
            null,
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnValue("f")))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/13"), LogSequenceNumber.valueOf("0/14")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/16"), 8));
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
            LogSequenceNumber.valueOf("0/16"), LogSequenceNumber.valueOf("0/17")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/19"), 9));
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
            LogSequenceNumber.valueOf("0/19"), LogSequenceNumber.valueOf("0/1A")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/1D"), 10));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("3"),
              new PgOutputMessageTupleColumnValue("ghij"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputUpdateMessage.CreateForComparison(
            // No before image in DEFAULT, so old tuple comes out as null.
            null,
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("updated_ghij"),
                new PgOutputMessageTupleColumnValue("f")))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/1D"), LogSequenceNumber.valueOf("0/1E")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/21"), 11));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("4"),
              new PgOutputMessageTupleColumnValue("jklm"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("4"),
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnNull()))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/21"), LogSequenceNumber.valueOf("0/22")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/25"), 12));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("3"),
              new PgOutputMessageTupleColumnValue("ghij"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputUpdateMessage.CreateForComparison(
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                // All columns for before image in FULL come out as null.
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnNull())),
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("updated_ghij"),
                new PgOutputMessageTupleColumnValue("f")))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/25"), LogSequenceNumber.valueOf("0/26")));

          add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/29"), 13));
          add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("4"),
              new PgOutputMessageTupleColumnValue("jklm"),
              new PgOutputMessageTupleColumnValue("t")))));
          add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
            new PgOutputMessageTuple((short) 3,
              Arrays.asList(
                new PgOutputMessageTupleColumnValue("4"),
                new PgOutputMessageTupleColumnNull(),
                new PgOutputMessageTupleColumnNull()))));
          add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/29"), LogSequenceNumber.valueOf("0/2A")));
        }
      };
    }
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testReplicationConnectionConsumptionWithCreateIndex() throws Exception {
    Map<String, String> tserverFlags = super.getTServerFlags();
    tserverFlags.put("ysql_yb_wait_for_backends_catalog_version_timeout", "10000");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    final String slotName = "test_slot";
    final String pluginName = YB_OUTPUT_PLUGIN_NAME;

    try (Statement stmt = connection.createStatement()) {
      // Drop the table if it exists to ensure that we are starting with a clean slate.
      // This is important as we have seen previous instances where if an earlier tests
      // fails without dropping the table, it causes a cascading failure for other tests
      // which use the same table name.
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text, c bool)");

      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    PGReplicationConnection replConnection = getConnectionBuilder().withTServer(0)
                                                .replicationConnect()
                                                .unwrap(PGConnection.class)
                                                .getReplicationAPI();

    createSlot(replConnection, slotName, pluginName);

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO t1 VALUES (generate_series(1, 1000), 'lmno', false)");
      stmt.execute("ALTER TABLE t1 ADD COLUMN txt_col TEXT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 1 Relation, 1 + 1000 + 1 (begin, 1000 inserts, commit), 1 Relation.
    final int totalMessagesBeforeIndexCreation = 1004;

    // We will only consume a few records and then create an index on the table without
    // consuming the ALTER (RELATION) message. This will test that the index creation
    // process is not blocked by the catalog version change.
    result.addAll(receiveMessage(stream, totalMessagesBeforeIndexCreation - 1 /* RELATION */));

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE INDEX idx_t1 ON t1(a)");
      stmt.execute("INSERT INTO t1 VALUES (1001, 'pqrs', true, 'text_col_val')");
    }

    // Consume the remaining messages.
    result.addAll(receiveMessage(stream, 4 /* RELATION + BEGIN + INSERT + COMMIT */));

    // Check for the expected messages count, we are not verifying the values of the
    // messages here as there are plenty of other tests which do so and this test is
    // specifically meant to verify the index creation process.
    // There will be total totalMessagesBeforeIndexCreation + 3 messages in the stream.
    // 1 Relation, 1 + 1000 + 1 (Begin, 1000 Inserts, Commit),
    // 1 Relation, 1 + 1 + 1 (Begin, Insert, Commit)
    assertEquals(totalMessagesBeforeIndexCreation + 3, result.size());

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

  void testDynamicTableAdditionForAllTablesPublication(boolean usePubRefresh) throws Exception {
    String slotName = "test_dynamic_table_addition_for_all_tables_pub";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("DROP TABLE IF EXISTS t3");
      stmt.execute("DROP TABLE IF EXISTS t4");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t2 (a int, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
      getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

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
      stmt.execute("CREATE TABLE t4 (a int, b text)");
    }

    if (usePubRefresh) {
      Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(3, 'mnop')");
      stmt.execute("INSERT INTO t2 VALUES(4, 'qrst')");
      stmt.execute("INSERT INTO t3 values(5, 'uvwx')");
      stmt.execute("INSERT INTO t4 values(6, 'wxyz')");
      stmt.execute("COMMIT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 6 from first txn and 8 from second txn.
    result.addAll(receiveMessage(stream, 14));

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
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/B"), 3));
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
        // insert 4
        add(PgOutputRelationMessage.CreateForComparison("public", "t4", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("6"),
            new PgOutputMessageTupleColumnValue("wxyz")))));
        // commit
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/B"), LogSequenceNumber.valueOf("0/C")));
      }
    };

    assertEquals(expectedResult, result);

    stream.close();
  }

  public void setFlagsForDynamicTablesTest(Map<String, String> tserverFlags,
      Map<String, String> masterFlags, Boolean usePubRefresh, Boolean streamTablesWithoutPrimaryKey)
      throws Exception {
    tserverFlags.put("allowed_preview_flags_csv",
        "ysql_yb_enable_implicit_dynamic_tables_logical_replication,"
            + "ysql_yb_cdcsdk_stream_tables_without_primary_key");
    tserverFlags.put("cdcsdk_enable_dynamic_table_support", "" + usePubRefresh);
    tserverFlags.put(
        "ysql_yb_enable_implicit_dynamic_tables_logical_replication", "" + !usePubRefresh);
    tserverFlags.put(
        "ysql_yb_cdcsdk_stream_tables_without_primary_key", "" + streamTablesWithoutPrimaryKey);

    masterFlags.put("allowed_preview_flags_csv",
        "ysql_yb_enable_implicit_dynamic_tables_logical_replication,"
            + "ysql_yb_cdcsdk_stream_tables_without_primary_key");
    masterFlags.put(
        "ysql_yb_enable_implicit_dynamic_tables_logical_replication", "" + !usePubRefresh);
    masterFlags.put(
        "ysql_yb_cdcsdk_stream_tables_without_primary_key", "" + streamTablesWithoutPrimaryKey);

    restartClusterWithFlags(masterFlags, tserverFlags);
  }

  @Test
  public void testDynamicTableAdditionForAllTablesPublicationWithPubRefresh()
      throws Exception {
    setFlagsForDynamicTablesTest(super.getTServerFlags(), super.getMasterFlags(),
        true /* usePubRefresh */, true /* streamTablesWithoutPrimaryKey */);
    testDynamicTableAdditionForAllTablesPublication(true /* usePubRefresh */);
  }

  @Test
  public void testDynamicTableAdditionForAllTablesPublicationWithoutPubRefresh()
      throws Exception {
    setFlagsForDynamicTablesTest(super.getTServerFlags(), super.getMasterFlags(),
        false /* usePubRefresh */, true /* streamTablesWithoutPrimaryKey */);
    testDynamicTableAdditionForAllTablesPublication(false /* usePubRefresh */);
  }

  void testDynamicTableAdditionForTablesCreatedBeforeStreamCreation(boolean usePubRefresh)
      throws Exception {
    String slotName = "test_dynamic_table_addition_slot_before_stream_creation";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("DROP TABLE IF EXISTS t3");
      stmt.execute("DROP TABLE IF EXISTS t4");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t2 (a int, b text)");
      stmt.execute("CREATE TABLE t3 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t4 (a int, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1, t2");
    }

    Connection conn =
      getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO t2 VALUES(2, 'defg')");
      stmt.execute("INSERT INTO t3 values(3, 'ghij')");
      stmt.execute("INSERT INTO t4 values(4, 'jklm')");
      stmt.execute("COMMIT");
      stmt.execute("ALTER PUBLICATION pub ADD TABLE t3");
      stmt.execute("ALTER PUBLICATION pub ADD TABLE t4");
    }

    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    if (usePubRefresh) {
      Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(3, 'mnop')");
      stmt.execute("INSERT INTO t2 VALUES(4, 'qrst')");
      stmt.execute("INSERT INTO t3 values(5, 'uvwx')");
      stmt.execute("INSERT INTO t4 values(6, 'wxyz')");
      stmt.execute("COMMIT");
      stmt.execute("ALTER PUBLICATION pub DROP TABLE t1");
      stmt.execute("ALTER PUBLICATION pub DROP TABLE t2");

      if (usePubRefresh) {
        Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
      }

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(6, 'ijkl')");
      stmt.execute("INSERT INTO t2 VALUES(7, 'lmno')");
      stmt.execute("INSERT INTO t3 values(8, 'opqr')");
      stmt.execute("INSERT INTO t4 values(9, 'rstu')");
      stmt.execute("COMMIT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 6 from first txn, 8 from second txn, 4 from third txn.
    result.addAll(receiveMessage(stream, 18));

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

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/B"), 3));
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
        add(PgOutputRelationMessage.CreateForComparison("public", "t4", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("6"),
            new PgOutputMessageTupleColumnValue("wxyz")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/B"), LogSequenceNumber.valueOf("0/C")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/F"), 4));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("8"),
            new PgOutputMessageTupleColumnValue("opqr")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("9"),
            new PgOutputMessageTupleColumnValue("rstu")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/F"), LogSequenceNumber.valueOf("0/10")));
      }
    };

    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testDynamicTableAdditionForTablesCreatedBeforeStreamCreationWithPubRefresh()
      throws Exception {
    setFlagsForDynamicTablesTest(super.getTServerFlags(), super.getMasterFlags(),
        true /* usePubRefresh */, true /* streamTablesWithoutPrimaryKey */);
    testDynamicTableAdditionForTablesCreatedBeforeStreamCreation(true /* usePubRefresh */);
  }

  @Test
  public void testDynamicTableAdditionForTablesCreatedBeforeStreamCreationWithoutPubRefresh()
      throws Exception {
    setFlagsForDynamicTablesTest(super.getTServerFlags(), super.getMasterFlags(),
        false /* usePubRefresh */, true /* streamTablesWithoutPrimaryKey */);
    testDynamicTableAdditionForTablesCreatedBeforeStreamCreation(false /* usePubRefresh */);
  }

  void testNonPKTablesNeverPolledForStreamCreatedWhenFlagUnset(boolean usePubRefresh)
      throws Exception {
    String slotName = "test_non_pk_tables_never_polled_for_stream_created_when_flag_unset";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("DROP TABLE IF EXISTS t3");
      stmt.execute("DROP TABLE IF EXISTS t4");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t2 (a int, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

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
    }

    for (HostAndPort tServer : miniCluster.getTabletServers().keySet()) {
      miniCluster.getClient().setFlag(
          tServer, "allowed_preview_flags_csv", "ysql_yb_cdcsdk_stream_tables_without_primary_key");
      miniCluster.getClient().setFlag(
          tServer, "ysql_yb_cdcsdk_stream_tables_without_primary_key", "true");
    }

    for (HostAndPort master : miniCluster.getMasters().keySet()) {
      miniCluster.getClient().setFlag(
          master, "allowed_preview_flags_csv", "ysql_yb_cdcsdk_stream_tables_without_primary_key");
      miniCluster.getClient().setFlag(
          master, "ysql_yb_cdcsdk_stream_tables_without_primary_key", "true");
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t3 (a int, b text)");
      stmt.execute("CREATE TABLE t4 (a int primary key, b text)");

      if (usePubRefresh) {
        Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
      }

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(3, 'mnop')");
      stmt.execute("INSERT INTO t2 VALUES(4, 'qrst')");
      stmt.execute("INSERT INTO t3 values(5, 'uvwx')");
      stmt.execute("INSERT INTO t4 values(6, 'wxyz')");
      stmt.execute("COMMIT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 4 from first txn and 5 from second txn.
    result.addAll(receiveMessage(stream, 9));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/8"), 3));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("mnop")))));
        add(PgOutputRelationMessage.CreateForComparison("public", "t4", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("6"),
                new PgOutputMessageTupleColumnValue("wxyz")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/8"), LogSequenceNumber.valueOf("0/9")));
      }
    };

    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testNonPKTablesNeverPolledForStreamCreatedWhenFlagUnsetWithPubRefresh()
      throws Exception {
    setFlagsForDynamicTablesTest(super.getTServerFlags(), super.getMasterFlags(),
        true /* usePubRefresh */, false /* streamTablesWithoutPrimaryKey */);
    testNonPKTablesNeverPolledForStreamCreatedWhenFlagUnset(true /* usePubRefresh */);
  }

  @Test
  public void testNonPKTablesNeverPolledForStreamCreatedWhenFlagUnsetWithoutPubRefresh()
      throws Exception {
    setFlagsForDynamicTablesTest(super.getTServerFlags(), super.getMasterFlags(),
        false /* usePubRefresh */, false /* streamTablesWithoutPrimaryKey */);
    testNonPKTablesNeverPolledForStreamCreatedWhenFlagUnset(false /* usePubRefresh */);
  }

  @Test
  public void replicationConnectionConsumptionWithYboutput() throws Exception {
    testReplicationConnectionConsumption("test_repl_slot_consumption", YB_OUTPUT_PLUGIN_NAME);
  }

  @Test
  public void replicationConnectionConsumptionWithPgoutput() throws Exception {
    testReplicationConnectionConsumption("test_repl_slot_consumption", PG_OUTPUT_PLUGIN_NAME);
  }

  @Test
  public void replicationConnectionConsumptionMultipleBatchesWithYboutput() throws Exception {
    Map<String, String> tserverFlags = super.getTServerFlags();
    // Set the batch size to a smaller value than the default of 500, so that the test is fast.
    tserverFlags.put("cdcsdk_max_consistent_records", "2");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    testReplicationConnectionConsumption("test_repl_slot_consumption_mul_batches",
                                         YB_OUTPUT_PLUGIN_NAME);
  }

  @Test
  public void replicationConnectionConsumptionMultipleBatchesWithPgoutput() throws Exception {
    Map<String, String> tserverFlags = super.getTServerFlags();
    // Set the batch size to a smaller value than the default of 500, so that the test is fast.
    tserverFlags.put("cdcsdk_max_consistent_records", "2");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    testReplicationConnectionConsumption("test_repl_slot_consumption_mul_batches",
                                         PG_OUTPUT_PLUGIN_NAME);
  }

  @Test
  public void replicationConnectionConsumptionAllDataTypesWithYbOutput() throws Exception {
    replicationConnectionConsumptionAllDataTypes(YB_OUTPUT_PLUGIN_NAME);
  }

  @Test
  public void replicationConnectionConsumptionAllDataTypesWithPgOutput() throws Exception {
    replicationConnectionConsumptionAllDataTypes(PG_OUTPUT_PLUGIN_NAME);
  }

  void replicationConnectionConsumptionAllDataTypes(String pluginName) throws Exception {
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
        + "col_hstore HSTORE, "
        + "col_discount coupon_discount_type, "
        +" col_discount_array coupon_discount_type[])";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE EXTENSION IF NOT EXISTS hstore;");
      stmt.execute("CREATE TYPE coupon_discount_type AS ENUM ('FIXED', 'PERCENTAGE');");
      stmt.execute(create_stmt);
      if (pluginName.equals(PG_OUTPUT_PLUGIN_NAME)) {
        stmt.execute("ALTER TABLE test_table REPLICA IDENTITY DEFAULT");
      }
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(
        replConnection, "test_slot_repl_conn_all_data_types", pluginName);

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
          + "'[2024-01-01, 2024-12-31)','key1 => value1, key2 => value2'::hstore, 'FIXED', "
          + "array['FIXED', 'PERCENTAGE']::coupon_discount_type[]);");
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
    result.addAll(receiveMessage(stream, 7));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputTypeMessage.CreateForComparison("public", "hstore"));
        add(PgOutputTypeMessage.CreateForComparison("public", "coupon_discount_type"));
        add(PgOutputTypeMessage.CreateForComparison("public", "_coupon_discount_type"));
        if (pluginName.equals(YB_OUTPUT_PLUGIN_NAME)) {
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
              // The Oids for columns below are not fixed. Changing the order of creation of
              // objects (extensions, tables etc.) in the test will change these Oids. Hence,
              // skip comparing the Oids of these types.
              PgOutputRelationMessageColumn.CreateForComparison("col_hstore"),
              PgOutputRelationMessageColumn.CreateForComparison("col_discount"),
              PgOutputRelationMessageColumn.CreateForComparison("col_discount_array"))));
        } else {
          // The replica identity for test_table in case of pgoutput is DEFAULT.
          add(PgOutputRelationMessage.CreateForComparison("public", "test_table", 'd',
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
              // The Oids for columns below are not fixed. Changing the order of creation of
              // objects (extensions, tables etc.) in the test will change these Oids. Hence,
              // skip comparing the Oids of these types.
              PgOutputRelationMessageColumn.CreateForComparison("col_hstore"),
              PgOutputRelationMessageColumn.CreateForComparison("col_discount"),
              PgOutputRelationMessageColumn.CreateForComparison("col_discount_array"))));
        }
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 38,
            Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("110110"),
                new PgOutputMessageTupleColumnValue("t"),
                new PgOutputMessageTupleColumnValue("(1,1),(0,0)"),
                new PgOutputMessageTupleColumnValue("\\x012345"),
                new PgOutputMessageTupleColumnValue("127.0.0.1/32"),
                new PgOutputMessageTupleColumnValue("<(0,0),1>"),
                new PgOutputMessageTupleColumnValue("2024-02-01"),
                new PgOutputMessageTupleColumnValue("1.201"),
                new PgOutputMessageTupleColumnValue("3.14"),
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
                new PgOutputMessageTupleColumnValue("\"key1\"=>\"value1\", \"key2\"=>\"value2\""),
                new PgOutputMessageTupleColumnValue("FIXED"),
                new PgOutputMessageTupleColumnValue("{FIXED,PERCENTAGE}")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void consumptionOnSubsetOfColocatedTables() throws Exception {
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

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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
    // respectively) + 4 records/txn (B+I1+I2+C) * 1 multi-shard txn.
    // Note that empty transactions are skipped by pgoutput.
    result.addAll(receiveMessage(stream, 12));
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
    Map<String, String> tserverFlags = super.getTServerFlags();
    tserverFlags.put("ysql_yb_enable_replication_slot_consumption", "false");
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
        .withOutputPlugin(YB_OUTPUT_PLUGIN_NAME)
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
    createSlot(
        replConnection, "test_slot_repl_conn_attribute_dropped", YB_OUTPUT_PLUGIN_NAME);

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
    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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
    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

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

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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

  // The reorderbuffer spills transactions with more than yb_reorderbuffer_max_changes_in_memory
  // changes on the disk. This test asserts that such transactions also work correctly.
  @Test
  public void testReplicationWithSpilledTransaction() throws Exception {
    // Set the value of ysql_yb_reorderbuffer_max_changes_in_memory to 1000.
    // The default value of the flag is 4096.
    Set<HostAndPort> tServers = miniCluster.getTabletServers().keySet();
    for (HostAndPort tServer : tServers) {
      setServerFlag(tServer, "ysql_yb_reorderbuffer_max_changes_in_memory", "1000");
    }
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

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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
        null,
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

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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
        null,
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

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
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

    createSlot(replConnection, slotName, "test_decoding");
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
    result.addAll(receiveStringMessages(stream, 36));

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
        add("table public.t1: UPDATE: a[integer]:1 b[text]:'updated_abcd'" +
                " c[boolean]:unchanged-toast-datum");
        add("COMMIT 5");

        add("BEGIN 6");
        add("table public.t1: UPDATE: a[integer]:2 b[text]:null " +
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
        add("table public.t3: UPDATE: a[integer]:1 b[text]:'updated_abcd' " +
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

  @Test
  public void testDynamicTableAdditionForTablesCreatedAfterStreamCreation() throws Exception {
    String slotName = "test_dynamic_table_addition_slot_after_stream_creation";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t2 (a int primary key, b text)");
      stmt.execute("ALTER PUBLICATION pub ADD TABLE t2");
    }

    PGReplicationStream stream = replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .start();

    Thread.sleep(kMultiplier * kPublicationRefreshIntervalSec * 2 * 1000);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t2 VALUES(3, 'ijkl')");
      stmt.execute("COMMIT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream, 4));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t2", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("ijkl")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testDDLWithDynamicTableAddition() throws Exception {
    String slotName = "test_ddl_with_dynamic_table_addition_slot";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("DROP TABLE IF EXISTS t2");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE TABLE t2 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn =
      getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO t2 VALUES(2, 'defg')");
      stmt.execute("COMMIT");

      stmt.execute("ALTER TABLE t1 DROP COLUMN b");

      stmt.execute("ALTER PUBLICATION pub ADD TABLE t2");
    }

    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    Thread.sleep(kMultiplier * kPublicationRefreshIntervalSec * 2 * 1000);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(3)");
      stmt.execute("INSERT INTO t2 VALUES(4, 'qrst')");
      stmt.execute("COMMIT");

      stmt.execute("ALTER TABLE t2 ADD COLUMN c int");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(5)");
      stmt.execute("INSERT INTO t2 VALUES(6, 'uvwx', 10)");
      stmt.execute("COMMIT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream, 15));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnValue("abcd")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/8"), 3));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 1,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("3")))));
        add(PgOutputRelationMessage.CreateForComparison("public", "t2", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("4"),
            new PgOutputMessageTupleColumnValue("qrst")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/8"), LogSequenceNumber.valueOf("0/9")));


        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/C"), 4));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 1,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("5")))));
        add(PgOutputRelationMessage.CreateForComparison("public", "t2", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 25),
            PgOutputRelationMessageColumn.CreateForComparison("c", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("6"),
            new PgOutputMessageTupleColumnValue("uvwx"),
            new PgOutputMessageTupleColumnValue("10")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/C"), LogSequenceNumber.valueOf("0/D")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testDDLWithRestart() throws Exception {
    String slotName = "test_ddl_with_restart_slot";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    // To avoid publication refresh, set the flag to a very high value.
    Set<HostAndPort> tServers = miniCluster.getTabletServers().keySet();
    for (HostAndPort tServer : tServers) {
      setServerFlag(tServer, "cdcsdk_publication_list_refresh_interval_secs", "10000");
      setServerFlag(tServer, "cdc_state_checkpoint_update_interval_ms", "0");
    }
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("ALTER TABLE t1 ADD COLUMN c int");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd', 10)");
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
    // 1 BEGIN, 1 RELATION, 1 INSERT, 1 COMMIT
    result.addAll(receiveMessage(stream, 4));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25),
                PgOutputRelationMessageColumn.CreateForComparison("c", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd"),
                new PgOutputMessageTupleColumnValue("10")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));
      }
    };

    assertEquals(expectedResult, result);

    // Send feedback for txn1.
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();
    waitForRestartLSN(connection, slotName, 4L);
    // Before we restart, wait for the VWAL to make GetChanges call on each of the 3
    // tablets with the latest explicit checkpoint. Since, during these GetConsistentChanges calls,
    // response received will be empty, Walsender will sleep for 1s before sending the next call.
    // Therefore, on the safe side, we will wait for triple the time taken to make GetChanges on
    // all 3 tablets i.e. 3 * (3 * 1s (sleep by WS))
    Thread.sleep(kMultiplier * 3 * 3 * 1000);
    stream.close();
    conn.close();

     // Restart Walsender
    conn = getConnectionBuilder().withTServer(0).replicationConnect();
    replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
     stmt.execute("INSERT INTO t1 VALUES(2, 'xyz', 20)");
     stmt.execute("COMMIT");
    }

    stream = replConnection.replicationStream()
       .logical()
       .withSlotName(slotName)
       .withStartPosition(LogSequenceNumber.valueOf(5L))
       .withSlotOption("proto_version", 1)
       .withSlotOption("publication_names", "pub")
       .start();


    result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream, 4));

    expectedResult = new ArrayList<PgOutputMessage>() {
     {
       add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/7"), 3));
       add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'c',
           Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
               PgOutputRelationMessageColumn.CreateForComparison("b", 25),
               PgOutputRelationMessageColumn.CreateForComparison("c", 23))));
       add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
           Arrays.asList(
               new PgOutputMessageTupleColumnValue("2"),
               new PgOutputMessageTupleColumnValue("xyz"),
               new PgOutputMessageTupleColumnValue("20")))));
       add(PgOutputCommitMessage.CreateForComparison(
           LogSequenceNumber.valueOf("0/7"), LogSequenceNumber.valueOf("0/8")));
     }
   };
   assertEquals(expectedResult, result);
   stream.close();
  }

  @Test
  public void testWithWal2JsonPlugin() throws Exception {
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

    String slotName = "test_with_wal2json";
    Connection conn =
      getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, "wal2json");
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
    result.addAll(receiveStringMessages(stream, 12));

    List<String> expectedResult = new ArrayList<String>() {
      {
        add(
          "{\"xid\":2,\"change\":[{\"kind\":\"insert\",\"schema\":\"public\",\"table\":\"t1\","
          +"\"columnnames\":[\"a\",\"b\",\"c\"],\"columntypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"columnvalues\":[1,\"abcd\",true]}]}"
          );
        add(
          "{\"xid\":3,\"change\":[{\"kind\":\"insert\",\"schema\":\"public\",\"table\":\"t1\","
          +"\"columnnames\":[\"a\",\"b\",\"c\"],\"columntypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"columnvalues\":[2,\"defg\",true]}]}"
        );
        add(
          "{\"xid\":4,\"change\":[{\"kind\":\"insert\",\"schema\":\"public\",\"table\":\"t1\","
          +"\"columnnames\":[\"a\",\"b\",\"c\"],\"columntypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"columnvalues\":[3,\"hijk\",false]}]}"
        );
        add(
          "{\"xid\":5,\"change\":[{\"kind\":\"update\",\"schema\":\"public\",\"table\":\"t1\","
          +"\"columnnames\":[\"a\",\"b\"],\"columntypes\":[\"integer\",\"text\"],"
          +"\"columnvalues\":[1,\"updated_abcd\"]}]}"
        );
        add(
          "{\"xid\":6,\"change\":[{\"kind\":\"update\",\"schema\":\"public\",\"table\":\"t1\","
          +"\"columnnames\":[\"a\",\"b\",\"c\"],\"columntypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"columnvalues\":[2,null,false]}]}"
        );
        add(
          "{\"xid\":7,\"change\":[{\"kind\":\"delete\",\"schema\":\"public\",\"table\":\"t1\","
          +"\"oldkeys\":{\"keynames\":[\"a\"],\"keytypes\":[\"integer\"],\"keyvalues\":[2]}}]}"
        );
        add(
          "{\"xid\":8,\"change\":[{\"kind\":\"insert\",\"schema\":\"public\",\"table\":\"t2\","
          +"\"columnnames\":[\"a\",\"b\",\"c\"],\"columntypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"columnvalues\":[1,\"abcd\",true]}]}"
        );
        add(
          "{\"xid\":9,\"change\":[{\"kind\":\"update\",\"schema\":\"public\",\"table\":\"t2\","
          +"\"columnnames\":[\"a\",\"b\",\"c\"],\"columntypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"columnvalues\":[1,\"updated_abcd\",true],"
          +"\"oldkeys\":{\"keynames\":[\"a\",\"b\",\"c\"],"
          +"\"keytypes\":[\"integer\",\"text\",\"boolean\"],\"keyvalues\":[1,\"abcd\",true]}}]}"
        );
        add(
          "{\"xid\":10,\"change\":[{\"kind\":\"delete\","
          +"\"schema\":\"public\",\"table\":\"t2\","
          +"\"oldkeys\":{\"keynames\":[\"a\",\"b\",\"c\"],"
          +"\"keytypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"keyvalues\":[1,\"updated_abcd\",true]}}]}"
        );
        add(
          "{\"xid\":11,\"change\":[{\"kind\":\"insert\",\"schema\":\"public\",\"table\":\"t3\","
          +"\"columnnames\":[\"a\",\"b\",\"c\"],\"columntypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"columnvalues\":[1,\"abcd\",true]}]}"
        );
        add(
          "{\"xid\":12,\"change\":[{\"kind\":\"update\",\"schema\":\"public\",\"table\":\"t3\","
          +"\"columnnames\":[\"a\",\"b\",\"c\"],\"columntypes\":[\"integer\",\"text\",\"boolean\"],"
          +"\"columnvalues\":[1,\"updated_abcd\",true],"
          +"\"oldkeys\":{\"keynames\":[\"a\"],\"keytypes\":[\"integer\"],\"keyvalues\":[1]}}]}"
        );
        add(
          "{\"xid\":13,\"change\":[{\"kind\":\"delete\",\"schema\":\"public\",\"table\":\"t3\","
          +"\"oldkeys\":{\"keynames\":[\"a\"],\"keytypes\":[\"integer\"],\"keyvalues\":[1]}}]}"
        );
      }
    };

    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testReplicaIdentityChangeShouldNotWorkWithPgoutput() throws Exception {
    String slotName = "test_replica_identity_change_with_pgoutput";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");

      // CHANGE is the default but we do it explicitly so that the tests do not need changing if we
      // change the default.
      stmt.execute("ALTER TABLE t1 REPLICA IDENTITY CHANGE");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE t1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    try {
      // This will fail since pgoutput does not support replica identity CHANGE.
      createSlot(replConnection, slotName, "pgoutput");
    } catch (PSQLException e) {
      assertTrue(e.getMessage().contains("Replica identity CHANGE is not supported for output"
        + " plugin pgoutput. Consider using output plugin yboutput instead."));
    }
  }

  @Test
  public void testDynamicTableWithReplicaIdentityChangeWithPgoutput() throws Exception {
    String slotName = "test_dynamic_table_replica_identity_change_with_pgoutput";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("ALTER TABLE t1 REPLICA IDENTITY DEFAULT");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, "pgoutput");

    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd')");
      // Dynamic tables are added to the CDC stream just after creation by a background task.
      // The replica identity stored for these tables is determined by the GFlag
      // ysql_yb_default_replica_identity whose default value is CHANGE.
      stmt.execute("CREATE TABLE t2 (a int primary key, b text)");
    }

    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO t1 VALUES(3, 'mnop')");
      stmt.execute("INSERT INTO t2 VALUES(4, 'qrst')");
      stmt.execute("COMMIT");
    }

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    try {
      result.addAll(receiveMessage(stream, 12));
    } catch (PSQLException e) {
      assertTrue(e.getMessage().contains("replica identity CHANGE"));
      assertTrue(e.getMessage().contains("is not supported for output plugin pgoutput"));
    }
  }

  @Test
  public void testDefaultWalLevel() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      Row row = getSingleRow(stmt, "SHOW wal_level");
      assertEquals("logical", row.getString(0));
    }
  }

  @Test
  public void testWithAlterType() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
      stmt.execute("CREATE TABLE test (a int primary key, b int)");
      stmt.execute("ALTER TABLE test ALTER COLUMN b TYPE text");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "test_with_alter_type";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO test VALUES(2, 'defg')");
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

    result.addAll(receiveMessage(stream, 5));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/5"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd")))));
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
  public void testWithDropAddPk() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
      stmt.execute("CREATE TABLE test (a int primary key, b int)");
      stmt.execute("ALTER TABLE test DROP CONSTRAINT test_pkey");
      stmt.execute("ALTER TABLE test ADD CONSTRAINT test_b_pkey PRIMARY KEY (b)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "test_with_add_drop_pk";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(1, 3)");
      stmt.execute("INSERT INTO test VALUES(1, 4)");
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

    result.addAll(receiveMessage(stream, 5));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/5"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("3")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("4")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/5"), LogSequenceNumber.valueOf("0/6")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testWithTruncate() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
      stmt.execute("CREATE TABLE test (a int primary key, b int)");
      stmt.execute("INSERT INTO test VALUES(101, 103)");
      stmt.execute("INSERT INTO test VALUES(102, 104)");
      stmt.execute("TRUNCATE TABLE test");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "test_with_truncate";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(1, 3)");
      stmt.execute("INSERT INTO test VALUES(2, 4)");
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

    result.addAll(receiveMessage(stream, 5));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/5"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("3")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("4")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/5"), LogSequenceNumber.valueOf("0/6")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  // Added with the fix for #24308.
  @Test
  public void testWithDropTable() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_1");
      stmt.execute("DROP TABLE IF EXISTS test_2");
      stmt.execute("CREATE TABLE test_1 (a int primary key, b int)");
      stmt.execute("CREATE TABLE test_2 (a int primary key, b int)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE test_1");
    }

    String slotName = "test_with_drop_table";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE test_2");
      stmt.execute("INSERT INTO test_1 VALUES(1, 1)");
    }

    // Sleep for some time so that master background thread removes dropped table from the
    // replica identity map.
    Thread.sleep(3000 * kMultiplier);
    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();

    result.addAll(receiveMessage(stream, 4));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_1", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 23))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnValue("1")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testNullValueUpdates() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_1");
      stmt.execute("DROP TABLE IF EXISTS test_2");
      stmt.execute("CREATE TABLE test_1 (a INT PRIMARY KEY, b BIGINT, c text)");
      stmt.execute("CREATE TABLE test_2 (a INT PRIMARY KEY, b BIGINT, c text)");
      stmt.execute("ALTER TABLE test_1 REPLICA IDENTITY FULL");
      stmt.execute("ALTER TABLE test_2 REPLICA IDENTITY DEFAULT");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName = "test_null_value_update";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO test_1 (a, c) VALUES (1, 'abc')");
      stmt.execute("INSERT INTO test_2 (a, c) VALUES (1, 'abc')");

      stmt.execute("UPDATE test_1 SET b = NULL WHERE a = 1");
      stmt.execute("UPDATE test_2 SET b = NULL WHERE a = 1");

      stmt.execute("DELETE FROM test_1 WHERE a = 1");
      stmt.execute("DELETE FROM test_2 WHERE a = 1");
    }

    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();

    result.addAll(receiveMessage(stream, 20));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_1", 'f',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 20),
            PgOutputRelationMessageColumn.CreateForComparison("c", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnNull(),
            new PgOutputMessageTupleColumnValue("abc")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/7"), 3));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_2", 'd',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
            PgOutputRelationMessageColumn.CreateForComparison("b", 20),
            PgOutputRelationMessageColumn.CreateForComparison("c", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnNull(),
            new PgOutputMessageTupleColumnValue("abc")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/7"), LogSequenceNumber.valueOf("0/8")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 4));
        add(PgOutputUpdateMessage.CreateForComparison(
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              // All columns for before image in FULL, same as in PG.
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnNull(),
              new PgOutputMessageTupleColumnValue("abc"))),
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnNull(),
              new PgOutputMessageTupleColumnValue("abc")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/D"), 5));
        add(PgOutputUpdateMessage.CreateForComparison(
          null,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnNull(),
              new PgOutputMessageTupleColumnValue("abc")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/D"), LogSequenceNumber.valueOf("0/E")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/10"), 6));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ false,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnNull(),
              new PgOutputMessageTupleColumnValue("abc")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/10"), LogSequenceNumber.valueOf("0/11")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/13"), 7));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnNull(),
              new PgOutputMessageTupleColumnNull()))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/13"), LogSequenceNumber.valueOf("0/14")));
      }
    };
    assertEquals(expectedResult, result);

    stream.close();
  }

  @Test
  public void testYbGetCurrentHybridTimeLsn() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT yb_get_current_hybrid_time_lsn()");

      assertTrue(rs.next());
      assertEquals(1, rs.getMetaData().getColumnCount());
      assertEquals("int8", rs.getMetaData().getColumnTypeName(1));
    }
  }

  @Test
  public void testConsumptionOnSubsetOfTabletsFromMultipleSlots() throws Exception {
    Map<String, String> tserverFlags = super.getTServerFlags();
    tserverFlags.put(
            "allowed_preview_flags_csv", "ysql_yb_enable_consistent_replication_from_hash_range");
    tserverFlags.put("ysql_yb_enable_consistent_replication_from_hash_range", "true");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
      stmt.execute("CREATE TABLE test (a int primary key, b text) SPLIT INTO 2 tablets");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName1 = "test_slot_1";
    String slotName2 = "test_slot_2";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName1, YB_OUTPUT_PLUGIN_NAME);
    createSlot(replConnection, slotName2, YB_OUTPUT_PLUGIN_NAME);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test VALUES(1, 'abc')");
      stmt.execute("INSERT INTO test VALUES(2, 'def')");
      stmt.execute("INSERT INTO test VALUES(3, 'ghi')");
      stmt.execute("INSERT INTO test VALUES(4, 'jkl')");
      stmt.execute("INSERT INTO test VALUES(5, 'mno')");
      stmt.execute("COMMIT");
    }

    PGReplicationStream stream1 = replConnection.replicationStream()
        .logical()
        .withSlotName(slotName1)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        // test_slot_1 is expected to poll from tablet-1 whose start range is 0.
        .withSlotOption(HASH_RANGE_SLOT_OPTION, "0,32768")
        .start();

    // Transaction 1: 5 records (BEGIN, RELATION, INSERT (pk=1), INSERT(pk=5),
    // COMMIT)
    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream1, 5));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/5"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abc")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("5"),
                new PgOutputMessageTupleColumnValue("mno")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/5"), LogSequenceNumber.valueOf("0/6")));
      }
    };
    assertEquals(expectedResult, result);

    // Close this stream and the connection.
    stream1.close();
    conn.close();

    Connection conn2 = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection2 = conn2.unwrap(PGConnection.class).getReplicationAPI();

    PGReplicationStream stream2 = replConnection2.replicationStream()
        .logical()
        .withSlotName(slotName2)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        // test_slot_2 is expected to poll from tablet-2 whose start range is 32768.
        .withSlotOption(HASH_RANGE_SLOT_OPTION, "32768,65536")
        .start();

    // Transaction 1 - 6 records (BEGIN, RELATION, INSERT (pk=2), INSERT (pk=3),
    // INSERT (pk=4), COMMIT)
    result.clear();
    result.addAll(receiveMessage(stream2, 6));

    List<PgOutputMessage> expectedResult2 = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/6"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test", 'c',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("def")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("ghi")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("4"),
                new PgOutputMessageTupleColumnValue("jkl")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/6"), LogSequenceNumber.valueOf("0/7")));
      }
    };
    assertEquals(expectedResult2, result);

    stream2.close();
  }

  @Test
  public void testOutOfBoundHashRangeWithSlot() throws Exception {
    Map<String, String> tserverFlags = super.getTServerFlags();
    tserverFlags.put(
            "allowed_preview_flags_csv", "ysql_yb_enable_consistent_replication_from_hash_range");
    tserverFlags.put("ysql_yb_enable_consistent_replication_from_hash_range", "true");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    String slotName = "test_slot_1";
    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    String expectedErrorMessage = "hash_range out of bound";
    boolean exceptionThrown = false;
    try {
      replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .withSlotOption(HASH_RANGE_SLOT_OPTION, "32768,100000")
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
  public void testNonNumericHashRangeWithSlot() throws Exception {
    Map<String, String> tserverFlags = super.getTServerFlags();
    tserverFlags.put(
            "allowed_preview_flags_csv", "ysql_yb_enable_consistent_replication_from_hash_range");
    tserverFlags.put("ysql_yb_enable_consistent_replication_from_hash_range", "true");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    String slotName = "test_slot_1";
    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    String expectedErrorMessage = "invalid value for hash_range";
    boolean exceptionThrown = false;
    try {
      replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .withSlotOption(HASH_RANGE_SLOT_OPTION, "123abc,456def")
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
  public void testActivePidNull() throws Exception {
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    try (Statement statement = conn.createStatement()) {
      statement.execute(
            "SELECT * FROM pg_create_logical_replication_slot('test_slot', 'test_decoding')"
      );
    }
    try (Statement stmt = connection.createStatement()) {
      ResultSet res = stmt.executeQuery("SELECT active_pid FROM pg_replication_slots");
      assertTrue(res.next());
      Integer activePid = res.getObject("active_pid", Integer.class);
      assertNull(activePid);
      res.close();
    }
    conn.close();
  }

  @Test
  public void testActivePidAndWalStatusPopulationOnStreamRestart() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_1");
      stmt.execute("DROP TABLE IF EXISTS test_2");
      stmt.execute("CREATE TABLE test_1 (a int primary key, b int)");
      stmt.execute("CREATE TABLE test_2 (a int primary key, b int)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE test_1");
    }

    String slotName = "test_logical_replication_slot";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection =
      conn.unwrap(PGConnection.class).getReplicationAPI();
    replConnection.createReplicationSlot()
          .logical()
          .withSlotName(slotName)
          .withOutputPlugin(YB_OUTPUT_PLUGIN_NAME)
          .make();
    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();
    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    LogSequenceNumber lastLsn = stream.getLastReceiveLSN();
    try (Statement stmt = connection.createStatement()) {
      ResultSet res1 = stmt.executeQuery("SELECT pid FROM pg_stat_replication");
      int activePid1 = -2;
      assertTrue(res1.next());
      activePid1 = res1.getInt("pid");

      ResultSet res2 = stmt.executeQuery(String.format("SELECT * FROM pg_replication_slots"));
      int activePid2 = -1;
      assertTrue(res2.next());
      activePid2 = res2.getInt("active_pid");
      assertTrue(res2.getBoolean("active"));

      res2.close();
      assertEquals(activePid1, activePid2);
    }
    stream.close();
    conn.close();

    //Restarting connection
    conn = getConnectionBuilder().withTServer(0).replicationConnect();
    replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    try (Statement stmt = connection.createStatement()) {
      ResultSet res = stmt.executeQuery("SELECT active_pid FROM pg_replication_slots");
      assertTrue(res.next());
      int activePid = res.getInt("active_pid");
      assertTrue(res.wasNull());
      LOG.info(String.format("active_pid is %d", activePid));
      res.close();
    }

    //Creating new stream with same slot
    replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(lastLsn)
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();
    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    try (Statement stmt1 = connection.createStatement()) {
      ResultSet res1 = stmt1.executeQuery("SELECT * FROM pg_stat_replication");
      int activePid1 = -2;
      if (res1.next()) {
          activePid1 = res1.getInt("pid");
      }
      ResultSet res2 = stmt1.executeQuery(String.format("SELECT * FROM pg_replication_slots"));
      int activePid2 = -1;
      assertTrue(res2.next());
      activePid2 = res2.getInt("active_pid");
      assertTrue(res2.getBoolean("active"));
      String status = res2.getString("wal_status");
      assertEquals("reserved", status);

      res2.close();
      assertEquals(activePid1, activePid2);
    }
    conn.close();
  }

  @Test
  public void testActivePidPopulationFromDifferentTServers() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_1");
      stmt.execute("DROP TABLE IF EXISTS test_2");
      stmt.execute("CREATE TABLE test_1 (a int primary key, b int)");
      stmt.execute("CREATE TABLE test_2 (a int primary key, b int)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE test_1");
    }

    String slotName = "test_logical_replication_slot";
    Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
    Connection conn2 = getConnectionBuilder().withTServer(1).replicationConnect();
    Connection conn2_2 = getConnectionBuilder().withTServer(1).replicationConnect();
    Connection conn3 = getConnectionBuilder().withTServer(2).replicationConnect();
    //Creating slot on 1st TServer
    PGReplicationConnection replConnection1 =
      conn1.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection1, slotName, YB_OUTPUT_PLUGIN_NAME);
    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);

    //Acquiring slot on 2nd TServer
    PGReplicationConnection replConnection2 =
      conn2.unwrap(PGConnection.class).getReplicationAPI();
    PGReplicationStream stream = replConnection2.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();
    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);

    int activePid1 = -1;
    int activePid2 = -2;
    int activePid3 = -3;
    int activePid4 = -4;
    try (Statement stmt = conn2_2.createStatement()) {
      ResultSet res1 = stmt.executeQuery("SELECT * FROM pg_stat_replication");
      assertTrue(res1.next());
      activePid1 = res1.getInt("pid");

      ResultSet res2 = stmt.executeQuery(String.format("SELECT * FROM pg_replication_slots"));
      assertTrue(res2.next());
      activePid2 = res2.getInt("active_pid");
      assertTrue(res2.getBoolean("active"));

      res2.close();
      assertEquals(activePid1, activePid2);
    }

    try (Statement stmt = conn1.createStatement()) {
      ResultSet res = stmt.executeQuery(String.format("SELECT * FROM pg_replication_slots"));
      assertTrue(res.next());
      activePid3 = res.getInt("active_pid");

      assertEquals(activePid2, activePid3);
    }

    try (Statement stmt = conn3.createStatement()) {
      ResultSet res = stmt.executeQuery(String.format("SELECT * FROM pg_replication_slots"));
      assertTrue(res.next());
      activePid4 = res.getInt("active_pid");

      assertEquals(activePid2, activePid4);
    }
    conn1.close();
    conn2.close();
    conn2_2.close();
    conn3.close();
  }

  @Test
  public void testBackendXminAndStatePopulation() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_1");
      stmt.execute("DROP TABLE IF EXISTS test_2");
      stmt.execute("CREATE TABLE test_1 (a int primary key, b int)");
      stmt.execute("CREATE TABLE test_2 (a int primary key, b int)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE test_1");
    }

    String slotName = "test_logical_replication_slot";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection =
      conn.unwrap(PGConnection.class).getReplicationAPI();
    replConnection.createReplicationSlot()
          .logical()
          .withSlotName(slotName)
          .withOutputPlugin(YB_OUTPUT_PLUGIN_NAME)
          .make();
    replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();
    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    try (Statement stmt = connection.createStatement()) {
      ResultSet res1 = stmt.executeQuery("SELECT * FROM pg_stat_replication");
      int xmin1 = -2;
      assertTrue(res1.next());
      xmin1 = res1.getInt("backend_xmin");
      String state = res1.getString("state");

      assertEquals("streaming", state);

      ResultSet res2 = stmt.executeQuery(String.format("SELECT * FROM pg_replication_slots"));
      int xmin2 = -1;
      assertTrue(res2.next());
      xmin2 = res2.getInt("xmin");

      res2.close();
      assertEquals(xmin2, xmin1);
    }
    conn.close();
  }

  @Test
  public void testWalStatusLost() throws Exception {
    Map<String, String> tserverFlags = super.getTServerFlags();
    tserverFlags.put("cdc_intent_retention_ms", "0");
    restartClusterWithFlags(Collections.emptyMap(), tserverFlags);

    String slotName = "test_logical_replication_slot";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection =
      conn.unwrap(PGConnection.class).getReplicationAPI();
    replConnection.createReplicationSlot()
          .logical()
          .withSlotName(slotName)
          .withOutputPlugin(YB_OUTPUT_PLUGIN_NAME)
          .make();
    replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    try (Statement stmt = connection.createStatement()) {
      ResultSet res1 = stmt.executeQuery(String.format("SELECT * FROM pg_replication_slots"));
      assertTrue(res1.next());
      String status = res1.getString("wal_status");
      assertEquals("lost", status);
    }
    conn.close();
  }

  @Test
  public void testPgStatReplicationSlots() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE xyz (id int primary key)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }
    String slotName = "test_logical_replication_slot";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    PGReplicationStream stream = replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .start();
    try (Statement stmt = conn1.createStatement()) {
      stmt.execute("INSERT INTO xyz (id) SELECT generate_series(1,40000)");
    }
    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream, 40002));
    try (Statement stmt = conn1.createStatement()) {
      ResultSet r1 = stmt.executeQuery(String.format("SELECT * FROM pg_stat_replication_slots"));
      assertTrue(r1.next());
      long spill_txns = r1.getLong("spill_txns");
      long spill_count = r1.getLong("spill_count");
      long spill_bytes = r1.getLong("spill_bytes");
      long total_txns = r1.getLong("total_txns");
      long total_bytes = r1.getLong("total_bytes");

      assertEquals(1, spill_txns);
      assertEquals(2, spill_count); // ceil(spill_bytes/yb_reorderbuffer_max_changes_in_memory)
      assertEquals(5920000, spill_bytes); // 148*40000
      assertEquals(1, total_txns);
      assertEquals(5920000, total_bytes);

      stmt.execute("INSERT INTO xyz values (40001)");
      Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
      result.addAll(receiveMessage(stream, 3));
      r1 = stmt.executeQuery(String.format("SELECT * FROM pg_stat_replication_slots"));
      assertTrue(r1.next());
      spill_txns = r1.getLong("spill_txns");
      spill_count = r1.getLong("spill_count");
      spill_bytes = r1.getLong("spill_bytes");
      total_txns = r1.getLong("total_txns");
      total_bytes = r1.getLong("total_bytes");
      assertEquals(1, spill_txns);
      assertEquals(2, spill_count);
      assertEquals(5920000, spill_bytes);
      assertEquals(2, total_txns);
      assertEquals(5920148, total_bytes);

      // Reset the stat values
      stmt.executeQuery(String.format("SELECT pg_stat_reset_replication_slot(NULL)"));
      Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);

      ResultSet r2 = stmt.executeQuery(String.format("SELECT * FROM pg_stat_replication_slots"));
      assertTrue(r2.next());
      spill_txns = r2.getLong("spill_txns");
      spill_count = r2.getLong("spill_count");
      spill_bytes = r2.getLong("spill_bytes");
      total_txns = r2.getLong("total_txns");
      total_bytes = r2.getLong("total_bytes");
      assertEquals(0, spill_txns);
      assertEquals(0, spill_count);
      assertEquals(0, spill_bytes);
      assertEquals(0, total_txns);
      assertEquals(0, total_bytes);
    }
    stream.close();
    conn1.close();
    conn.close();
  }

  @Test
  public void testPgStatReplicationSlotsWithMultipleSlots() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE xyz (id int primary key)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    String slotName1 = "test_logical_replication_slot_1";
    String slotName2 = "test_logical_replication_slot_2";
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    Connection conn1 = getConnectionBuilder().withTServer(0).replicationConnect();
    Connection conn2 = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection1 = conn1.unwrap(PGConnection.class).getReplicationAPI();

    createSlot(replConnection1, slotName1, YB_OUTPUT_PLUGIN_NAME);

    List<PGReplicationStream> streams = new ArrayList<>();
    streams.add(replConnection1.replicationStream()
        .logical()
        .withSlotName(slotName1)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .start());
    try (Statement stmt = conn.createStatement()) {
      stmt.execute("INSERT INTO xyz (id) SELECT generate_series(1,40000)");
    }
    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(streams.get(0), 40002));

    PGReplicationConnection replConnection2 = conn2.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection2, slotName2, YB_OUTPUT_PLUGIN_NAME);

    streams.add(replConnection2.replicationStream()
        .logical()
        .withSlotName(slotName2)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .start());
    try (Statement stmt = conn.createStatement()) {
      stmt.execute("INSERT INTO xyz (id) SELECT generate_series(40001,80000)");
    }
    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);
    result.addAll(receiveMessage(streams.get(0), 40002));
    result.addAll(receiveMessage(streams.get(1), 40002));

    try (Statement stmt = conn.createStatement()) {
      ResultSet r1 = stmt.executeQuery(
        String.format("SELECT * FROM pg_stat_replication_slots WHERE slot_name='%s'", slotName1)
      );
      assertTrue(r1.next());
      long spill_txns = r1.getLong("spill_txns");
      long spill_count = r1.getLong("spill_count");
      long spill_bytes = r1.getLong("spill_bytes");
      long total_txns = r1.getLong("total_txns");
      long total_bytes = r1.getLong("total_bytes");

      assertEquals(2, spill_txns);
      assertEquals(4, spill_count);
      assertEquals(11840000, spill_bytes);
      assertEquals(2, total_txns);
      assertEquals(11840000, total_bytes);

      ResultSet r2 = stmt.executeQuery(
        String.format("SELECT * FROM pg_stat_replication_slots WHERE slot_name='%s'", slotName2)
      );
      assertTrue(r2.next());
      spill_txns = r2.getLong("spill_txns");
      spill_count = r2.getLong("spill_count");
      spill_bytes = r2.getLong("spill_bytes");
      total_txns = r2.getLong("total_txns");
      total_bytes = r2.getLong("total_bytes");

      assertEquals(1, spill_txns);
      assertEquals(2, spill_count);
      assertEquals(5920000, spill_bytes);
      assertEquals(1, total_txns);
      assertEquals(5920000, total_bytes);
    }
    for (PGReplicationStream stream : streams) {
      stream.close();
    }
    conn.close();
  }

  @Test
  public void testPgVectorWithLogicalReplication() throws Exception {
    final String slotName = "test_pgvector_cdc_slot";

    try (Statement stmt = connection.createStatement()) {
      // Drop table if exists and create vector extension
      stmt.execute("DROP TABLE IF EXISTS test_table");
      stmt.execute("CREATE EXTENSION IF NOT EXISTS vector");
      stmt.execute("CREATE TABLE test_table (id int primary key, text_col text, " +
          "vector_col vector)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE test_table");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      // Single shard insert.
      stmt.execute("INSERT INTO test_table VALUES (1, 'abc', '[123,234,345,456,567,789]')");

      // Multi shard insert.
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO test_table VALUES (2, 'xyz', ('[123,456]'))");
      stmt.execute("INSERT INTO test_table VALUES (3, 'xyz', ('[123,456]'))");
      stmt.execute("INSERT INTO test_table VALUES (4, 'xyz', ('[123,456]'))");
      stmt.execute("INSERT INTO test_table VALUES (5, 'xyz', ('[123,456]'))");
      stmt.execute("COMMIT");

      // Update PK.
      stmt.execute("UPDATE test_table SET id = 0 WHERE id = 1");

      // Update vector column.
      stmt.execute("UPDATE test_table SET  vector_col = ('[123,456,789]') WHERE id = 2");

      // Update non vector column.
      stmt.execute("UPDATE test_table SET text_col = 'xyz' WHERE id = 2");

      // Multi shard update.
      stmt.execute("BEGIN");
      stmt.execute("UPDATE test_table SET text_col = 'mno' WHERE id > 3");
      stmt.execute("UPDATE test_table SET  vector_col = ('[0,0,0,0,0,0,0]') WHERE id < 3");
      stmt.execute("COMMIT");

      // DDL to drop column.
      stmt.execute("ALTER TABLE test_table DROP COLUMN text_col");

      // Single shard delete.
      stmt.execute("DELETE FROM test_table WHERE id = 5");

      // DDL to add column.
      stmt.execute("ALTER TABLE test_table ADD COLUMN int_col int");

      // Multi shard delete.
      stmt.execute("DELETE FROM test_table WHERE id < 3");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream, 35));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        // Txn 1.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_table", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("id", 23),
            PgOutputRelationMessageColumn.CreateForComparison("text_col", 25),
            PgOutputRelationMessageColumn.CreateForComparison("vector_col", 8078))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnValue("abc"),
            new PgOutputMessageTupleColumnValue("[123,234,345,456,567,789]")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        // Txn 2.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 3));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("2"),
            new PgOutputMessageTupleColumnValue("xyz"),
            new PgOutputMessageTupleColumnValue("[123,456]")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("3"),
            new PgOutputMessageTupleColumnValue("xyz"),
            new PgOutputMessageTupleColumnValue("[123,456]")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("4"),
            new PgOutputMessageTupleColumnValue("xyz"),
            new PgOutputMessageTupleColumnValue("[123,456]")))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("5"),
            new PgOutputMessageTupleColumnValue("xyz"),
            new PgOutputMessageTupleColumnValue("[123,456]")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));

        // Txn 3.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/E"), 4));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnNull(),
              new PgOutputMessageTupleColumnNull()))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 3,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("0"),
            new PgOutputMessageTupleColumnValue("abc"),
            new PgOutputMessageTupleColumnValue("[123,234,345,456,567,789]")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/E"), LogSequenceNumber.valueOf("0/F")));

        // Txn 4.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/11"), 5));
        add(PgOutputUpdateMessage.CreateForComparison(null,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("2"),
              new PgOutputMessageTupleColumnToasted(),
              new PgOutputMessageTupleColumnValue("[123,456,789]")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/11"), LogSequenceNumber.valueOf("0/12")));

        // Txn 5.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/14"), 6));
        add(PgOutputUpdateMessage.CreateForComparison(null,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("2"),
              new PgOutputMessageTupleColumnValue("xyz"),
              new PgOutputMessageTupleColumnToasted()))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/14"), LogSequenceNumber.valueOf("0/15")));

        // Txn 6.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/1A"), 7));
        add(PgOutputUpdateMessage.CreateForComparison(null,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("4"),
              new PgOutputMessageTupleColumnValue("mno"),
              new PgOutputMessageTupleColumnToasted()))));
        add(PgOutputUpdateMessage.CreateForComparison(null,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("5"),
              new PgOutputMessageTupleColumnValue("mno"),
              new PgOutputMessageTupleColumnToasted()))));
        add(PgOutputUpdateMessage.CreateForComparison(null,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("0"),
              new PgOutputMessageTupleColumnToasted(),
              new PgOutputMessageTupleColumnValue("[0,0,0,0,0,0,0]")))));
        add(PgOutputUpdateMessage.CreateForComparison(null,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("2"),
              new PgOutputMessageTupleColumnToasted(),
              new PgOutputMessageTupleColumnValue("[0,0,0,0,0,0,0]")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/1A"), LogSequenceNumber.valueOf("0/1B")));

        // Txn 7.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/1D"), 8));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_table", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("id", 23),
            PgOutputRelationMessageColumn.CreateForComparison("vector_col", 8078))));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
          new PgOutputMessageTuple((short) 2,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("5"),
              new PgOutputMessageTupleColumnNull()))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/1D"), LogSequenceNumber.valueOf("0/1E")));

        // Txn 8.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/21"), 9));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_table", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("id", 23),
            PgOutputRelationMessageColumn.CreateForComparison("vector_col", 8078),
            PgOutputRelationMessageColumn.CreateForComparison("int_col", 23))));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("0"),
              new PgOutputMessageTupleColumnNull(),
              new PgOutputMessageTupleColumnNull()))));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
          new PgOutputMessageTuple((short) 3,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("2"),
              new PgOutputMessageTupleColumnNull(),
              new PgOutputMessageTupleColumnNull()))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/21"), LogSequenceNumber.valueOf("0/22")));
      }
    };

    for (PgOutputMessage message : result) {
      assertTrue("Message not found in expected results: " + message,
                 expectedResult.contains(message));
    }

    stream.close();
    conn.close();
  }

  public String getVectorString(int n, int value) {
    StringBuilder vectorSb = new StringBuilder("[");
    for (int i = 0; i < n; i++) {
      if (i > 0) {
        vectorSb.append(",");
      }
      vectorSb.append("" + value);
    }
    vectorSb.append("]");
    return vectorSb.toString();
  }

  @Test
  public void testLargeVectorWithLogicalReplication() throws Exception {
    final String slotName = "test_large_vector_cdc_slot";
    String vector2048Ones = getVectorString(2048, 1);
    String vector2048Twos = getVectorString(2048, 2);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_table");
      stmt.execute("CREATE EXTENSION IF NOT EXISTS vector");
      stmt.execute("CREATE TABLE test_table (id int primary key, vector_col vector)");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE test_table");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO test_table VALUES (1, '" + vector2048Ones + "')");
      stmt.execute("UPDATE test_table SET vector_col = '" + vector2048Twos + "' WHERE id = 1");
      stmt.execute("DELETE FROM test_table WHERE id = 1");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream, 10));

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        // Txn 1 - Insert.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_table", 'c',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("id", 23),
            PgOutputRelationMessageColumn.CreateForComparison("vector_col", 8078))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnValue(vector2048Ones)))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        // Txn 2 - Update.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/7"), 3));
        add(PgOutputUpdateMessage.CreateForComparison(null,
          new PgOutputMessageTuple((short) 2,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnValue(vector2048Twos)))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/7"), LogSequenceNumber.valueOf("0/8")));

        // Txn 3 - Delete.
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 4));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
          new PgOutputMessageTuple((short) 2,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnNull()))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));
      }
    };

    assertEquals(expectedResult, result);

    stream.close();
    conn.close();
  }

  @Test
  public void testPgoutputWithChangeReplicaIdentityNotInPublication() throws Exception {
    final String slotName = "test_pgoutput_and_change_slot";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_table_1");
      stmt.execute("DROP TABLE IF EXISTS test_table_2");
      stmt.execute("CREATE TABLE test_table_1 (id int primary key, name text)");
      stmt.execute("CREATE TABLE test_table_2 (id int primary key, name text)");
      stmt.execute("ALTER TABLE test_table_1 REPLICA IDENTITY DEFAULT");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE test_table_1");
    }

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, slotName, PG_OUTPUT_PLUGIN_NAME);

    // Sleep to ensure that pub refresh record is sent to the walsender before any DMLs.
    Thread.sleep(kPublicationRefreshIntervalSec * 2 * 1000);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO test_table_1 VALUES (1, 'abc')");
      stmt.execute("UPDATE test_table_1 SET name = 'def' WHERE id = 1");
      stmt.execute("DELETE FROM test_table_1 WHERE id = 1");
    }

    PGReplicationStream stream = replConnection.replicationStream()
      .logical()
      .withSlotName(slotName)
      .withStartPosition(LogSequenceNumber.valueOf(0L))
      .withSlotOption("proto_version", 1)
      .withSlotOption("publication_names", "pub")
      .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    result.addAll(receiveMessage(stream, 10));
    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "test_table_1", 'd',
          Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("id", 23),
            PgOutputRelationMessageColumn.CreateForComparison("name", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
          Arrays.asList(
            new PgOutputMessageTupleColumnValue("1"),
            new PgOutputMessageTupleColumnValue("abc")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/7"), 3));
        add(PgOutputUpdateMessage.CreateForComparison(
          // No before image in DEFAULT, so old tuple comes out as null.
          null,
          new PgOutputMessageTuple((short) 2,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnValue("def")))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/7"), LogSequenceNumber.valueOf("0/8")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 4));
        add(PgOutputDeleteMessage.CreateForComparison(/* hasKey */ true,
          new PgOutputMessageTuple((short) 2,
            Arrays.asList(
              new PgOutputMessageTupleColumnValue("1"),
              new PgOutputMessageTupleColumnNull()))));
        add(PgOutputCommitMessage.CreateForComparison(
          LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));
      }
    };
    assertEquals(expectedResult, result);

    // Close this stream and the connection.
    stream.close();
    conn.close();
  }

  @Test
  public void testRestartAfterDdl() throws Exception {
    Map<String, String> serverFlags = getTServerFlags();
    serverFlags.put("cdc_state_checkpoint_update_interval_ms", "0");
    restartClusterWithFlags(Collections.emptyMap(), serverFlags);

    final String slotName = "test_cdc_restart_after_ddl";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test_table");
      stmt.execute("CREATE TABLE test_table (id SERIAL PRIMARY KEY, "
                    + "text_col TEXT DEFAULT 'default_val')");
      stmt.execute("CREATE PUBLICATION pub FOR TABLE test_table");
    }

    // Create replication slot and initiate replication stream.
    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();
    createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

    PGReplicationStream stream = replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .start();

    // Insert a record and consume it.
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO test_table (text_col) VALUES ('first_record')");
    }

    // RELATION + BEGIN + INSERT + COMMIT.
    List<PgOutputMessage> firstInsertMessages = receiveMessage(stream, 4);
    assertEquals("Expected 4 messages for first insert", 4, firstInsertMessages.size());

    // Flush the LSN.
    stream.setFlushedLSN(stream.getLastReceiveLSN());
    stream.forceUpdateStatus();

    // Perform a DDL and insert a record with new schema.
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("ALTER TABLE test_table ADD COLUMN new_col INT DEFAULT 42");
      stmt.execute("INSERT INTO test_table (text_col, new_col) VALUES ('second_record', 100)");
    }

    // Consume the records (RELATION + BEGIN + INSERT + COMMIT). These records are
    // not acknowledged yet so we should receive them again after restart.
    List<PgOutputMessage> ddlAndInsertMessages = receiveMessage(stream, 4);
    assertEquals(4, ddlAndInsertMessages.size());

    // Sleep to ensure that walsender calls GetConsistentChanges.
    Thread.sleep(kMultiplier * 5 * 1000);

    // Close the stream and connection and then restart.
    stream.close();
    conn.close();

    conn = getConnectionBuilder().withTServer(0).replicationConnect();
    replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    stream = replConnection.replicationStream()
        .logical()
        .withSlotName(slotName)
        .withStartPosition(LogSequenceNumber.valueOf(0L))
        .withSlotOption("proto_version", 1)
        .withSlotOption("publication_names", "pub")
        .start();


    try (Statement stmt = connection.createStatement()) {
      stmt.execute("INSERT INTO test_table (text_col, new_col) VALUES ('third_record', 100)");
    }

    // Consume the records again (we should receive the DDL and second insert again).
    // i.e. RELATION + BEGIN + INSERT + COMMIT + BEGIN + INSERT + COMMIT.
    List<PgOutputMessage> restartMessages = receiveMessage(stream, 7);
    assertEquals(7, restartMessages.size());

    // Validate that we have received everything
    // Total expected messages: 4 (first relation + begin, insert1, commit)
    // + 4 (DDL + begin, insert2, commit) + 4 (DDL + begin, insert2, commit)
    // + 3 (begin, insert3, commit) = 15
    assertEquals(15,
        firstInsertMessages.size() + ddlAndInsertMessages.size() + restartMessages.size());

    stream.close();
    conn.close();
  }

  @Test
  public void testWal2jsonWithOriginId() throws Exception {
    final String slotName = "test_replication_with_origin_id";
    final String originId1 = "origin1";
    final String originId2 = "origin_2";

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS tbl1");
      stmt.execute("DROP TABLE IF EXISTS tbl2");
      stmt.execute("CREATE TABLE tbl1 (a INT PRIMARY KEY, b TEXT)");
      stmt.execute("CREATE TABLE tbl2 (a TEXT primary key) SPLIT INTO 2 TABLETS");
      stmt.execute("INSERT INTO tbl1 values (1,'a')");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");

      // Create 2 replication origins
      stmt.execute("SELECT pg_replication_origin_create('" + originId1 + "')");
      stmt.execute("SELECT pg_replication_origin_create('" + originId2 + "')");

      // Create replication slot
      createSlot(replConnection, slotName, WAL2JSON_PLUGIN_NAME);

      // Local insert
      stmt.execute("INSERT INTO tbl1 values (2,'b')");

      // Single tablet non distributed insert from origin 1
      stmt.execute("SELECT pg_replication_origin_session_setup('" + originId1 + "')");
      stmt.execute("INSERT INTO tbl2 values ('row1')");

      // Batch update from origin 2
      stmt.execute("SELECT pg_replication_origin_session_reset()");
      stmt.execute("SELECT pg_replication_origin_session_setup('" + originId2 + "')");
      stmt.execute("BEGIN");
      stmt.execute("UPDATE tbl1 SET a = a + 10");
      stmt.execute("COMMIT");

      // Transactional DML from origin 1
      stmt.execute("SELECT pg_replication_origin_session_reset()");
      stmt.execute("SELECT pg_replication_origin_session_setup('" + originId1 + "')");
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO tbl2 values ('row2')");
      stmt.execute("DELETE FROM tbl1 WHERE a = 12");
      stmt.execute("COMMIT");

      // Local delete
      stmt.execute("SELECT pg_replication_origin_session_reset()");
      stmt.execute("DELETE FROM tbl2 WHERE a = 'row2'");
    }

    // Consume the records
    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("include-origin", "1")
                                     .start();

    List<String> result = receiveStringMessages(stream, 5);
    assertEquals(5, result.size());
    JsonParser parser = new JsonParser();
    JsonObject jsonObject = parser.parse(result.get(0)).getAsJsonObject();
    // Change 1: Local insert
    LOG.info("JsonObjects: {}", jsonObject);
    validateChange(jsonObject, 0, 1);
    JsonArray changes = jsonObject.getAsJsonArray("change");
    JsonObject changeObject = changes.get(0).getAsJsonObject();
    assertEquals(changeObject.get("kind").getAsString(), "insert");
    assertEquals(changeObject.get("schema").getAsString(), "public");
    assertEquals(changeObject.get("table").getAsString(), "tbl1");
    JsonArray columnvalues = changeObject.getAsJsonArray("columnvalues");
    assertEquals(columnvalues.size(), 2);
    assertEquals(columnvalues.get(0).getAsInt(), 2);
    assertEquals(columnvalues.get(1).getAsString(), "b");

    // Change 2: Single tablet non distributed insert from origin 1
    jsonObject = parser.parse(result.get(1)).getAsJsonObject();
    validateChange(jsonObject, 1, 1);

    // Change 3: Batch update from origin 2
    jsonObject = parser.parse(result.get(2)).getAsJsonObject();
    validateChange(jsonObject, 2, 4);

    // Change 4: Transactional DML from origin 1
    jsonObject = parser.parse(result.get(3)).getAsJsonObject();
    validateChange(jsonObject, 1, 2);

    // Change 5: Local delete
    jsonObject = parser.parse(result.get(4)).getAsJsonObject();
    validateChange(jsonObject, 0, 1);

    stream.close();
    conn.close();
  }

  @Test
  public void testPgoutputWithOriginId() throws Exception {
    final String slotName = "test_replication_with_origin_id";
    final String originId1 = "origin1";
    final String originId2 = "origin_2";

    Connection conn = getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS tbl1");
      stmt.execute("DROP TABLE IF EXISTS tbl2");
      stmt.execute("CREATE TABLE tbl1 (a INT PRIMARY KEY, b TEXT)");
      stmt.execute("ALTER TABLE tbl1 REPLICA IDENTITY DEFAULT");
      stmt.execute("CREATE TABLE tbl2 (a TEXT primary key) SPLIT INTO 2 TABLETS");
      stmt.execute("ALTER TABLE tbl2 REPLICA IDENTITY DEFAULT");
      stmt.execute("INSERT INTO tbl1 values (1,'a')");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");

      // Create 2 replication origins
      stmt.execute("SELECT pg_replication_origin_create('" + originId1 + "')");
      stmt.execute("SELECT pg_replication_origin_create('" + originId2 + "')");

      // Create replication slot
      createSlot(replConnection, slotName, YB_OUTPUT_PLUGIN_NAME);

      // Local insert
      stmt.execute("INSERT INTO tbl1 values (2,'b')");

      // Single tablet non distributed insert from origin 1
      stmt.execute("SELECT pg_replication_origin_session_setup('" + originId1 + "')");
      stmt.execute("INSERT INTO tbl2 values ('row1')");

      // Batch update from origin 2
      stmt.execute("SELECT pg_replication_origin_session_reset()");
      stmt.execute("SELECT pg_replication_origin_session_setup('" + originId2 + "')");
      stmt.execute("BEGIN");
      stmt.execute("UPDATE tbl1 SET b = 'c' WHERE a = 1");
      stmt.execute("UPDATE tbl1 SET b = 'd' WHERE a = 2");
      stmt.execute("COMMIT");

      // Transactional DML from origin 1
      stmt.execute("SELECT pg_replication_origin_session_reset()");
      stmt.execute("SELECT pg_replication_origin_session_setup('" + originId1 + "')");
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO tbl2 values ('row2')");
      stmt.execute("DELETE FROM tbl1 WHERE a = 2");
      stmt.execute("COMMIT");

      // Local delete
      stmt.execute("SELECT pg_replication_origin_session_reset()");
      stmt.execute("DELETE FROM tbl2 WHERE a = 'row2'");
    }

    // Consume the records
    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> expectedResult = CreateMessages(
        // Change 1: Local insert
        PgOutputBeginMessage.Create("0/4", 2),
        PgOutputRelationMessage.Create("public", "tbl1", 'd',
            PgOutputRelationMessageColumn.Create("a", 23),
            PgOutputRelationMessageColumn.Create("b", 25)),
        PgOutputInsertMessage.Create("2", "b"),
        PgOutputCommitMessage.Create("0/4", "0/5"),

        // Change 2: Single tablet non distributed insert from origin 1
        PgOutputBeginMessage.Create("0/7", 3),
        PgOutputOriginMessage.Create(originId1),
        PgOutputRelationMessage.Create(
            "public", "tbl2", 'd', PgOutputRelationMessageColumn.Create("a", 25)),
        PgOutputInsertMessage.Create("row1"),
        PgOutputCommitMessage.Create("0/7", "0/8"),

        // Change 3: Insert from origin 1
        PgOutputBeginMessage.Create("0/B", 4),
        PgOutputOriginMessage.Create(originId2),
        PgOutputUpdateMessage.Create(true, "1", "c"),
        PgOutputUpdateMessage.Create(true, "2", "d"),
        PgOutputCommitMessage.Create("0/B", "0/C"),

        // Change 4: Transactional DML from origin 1
        PgOutputBeginMessage.Create("0/F", 5),
        PgOutputOriginMessage.Create(originId1),
        PgOutputInsertMessage.Create("row2"),
        PgOutputDeleteMessage.Create(true, "2", null),
        PgOutputCommitMessage.Create("0/F", "0/10"),

        // // Change 5: Local delete
        PgOutputBeginMessage.Create("0/12", 6),
        PgOutputDeleteMessage.Create(true, "row2"),
        PgOutputCommitMessage.Create("0/12", "0/13")
      );

    List<PgOutputMessage> result = receiveMessage(stream, 22);
    assertEquals(expectedResult, result);

    stream.close();
    conn.close();
  }
}
