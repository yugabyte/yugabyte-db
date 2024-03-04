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

import java.sql.Connection;
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

  @Override
  protected int getInitialNumTServers() {
    return 3;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("allowed_preview_flags_csv",
        "ysql_yb_enable_replication_commands,yb_enable_cdc_consistent_snapshot_streams");
    flagMap.put("ysql_yb_enable_replication_commands", "true");
    flagMap.put("ysql_TEST_enable_replication_slot_consumption", "true");
    flagMap.put("yb_enable_cdc_consistent_snapshot_streams", "true");
    flagMap.put("vmodule", "cdc_service=4,cdcsdk_producer=4");
    flagMap.put("max_clock_skew_usec", "" + kMaxClockSkewMs * 1000);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("allowed_preview_flags_csv",
        "ysql_yb_enable_replication_commands,yb_enable_cdc_consistent_snapshot_streams");
    flagMap.put("ysql_yb_enable_replication_commands", "true");
    flagMap.put("ysql_TEST_enable_replication_slot_consumption", "true");
    flagMap.put("yb_enable_cdc_consistent_snapshot_streams", "true");
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
      PGReplicationConnection replConnection, String slotName) throws Exception {
    replConnection.createReplicationSlot()
        .logical()
        .withSlotName(slotName)
        .withOutputPlugin("pgoutput")
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
    }

    return result;
  }

  // TODO(#20726): Add more test cases covering:
  // 1. INSERTs in a BEGIN/COMMIT block
  // 2. Single shard transactions
  // 3. Transactions with savepoints (commit/abort subtxns)
  // 4. Transactions after table rewrite operations like ADD PRIMARY KEY

  void testReplicationConnectionConsumption(String slotName) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS t1");
      stmt.execute("CREATE TABLE t1 (a int primary key, b text)");
      stmt.execute("CREATE PUBLICATION pub FOR ALL TABLES");
    }

    Connection conn =
        getConnectionBuilder().withTServer(0).replicationConnect();
    PGReplicationConnection replConnection = conn.unwrap(PGConnection.class).getReplicationAPI();

    createStreamAndWaitForSnapshotTimeToPass(replConnection, slotName);
    try (Statement stmt = connection.createStatement()) {
      // Do more than 2 inserts, since replicationConnectionConsumptionMultipleBatches tests the
      // case when #records > cdcsdk_max_consistent_records.
      stmt.execute("INSERT INTO t1 VALUES(1, 'abcd')");
      stmt.execute("INSERT INTO t1 VALUES(2, 'defg')");
      stmt.execute("INSERT INTO t1 VALUES(3, 'hijk')");
    }

    PGReplicationStream stream = replConnection.replicationStream()
                                     .logical()
                                     .withSlotName(slotName)
                                     .withStartPosition(LogSequenceNumber.valueOf(0L))
                                     .withSlotOption("proto_version", 1)
                                     .withSlotOption("publication_names", "pub")
                                     .start();

    List<PgOutputMessage> result = new ArrayList<PgOutputMessage>();
    // 1 Relation, 3 * 3 (begin, insert and commit).
    result.addAll(receiveMessage(stream, 10));
    for (PgOutputMessage res : result) {
      LOG.info("Row = {}", res);
    }

    // TODO(#20726): Add comments on the choice of LSN values once we have integrated with
    // GetConsistentChanges RPC. This requires the implementation of the LSN generator to be
    // completed.
    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'd',
            Arrays.asList(PgOutputRelationMessageColumn.CreateForComparison("a", 23),
                PgOutputRelationMessageColumn.CreateForComparison("b", 25))));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("1"),
                new PgOutputMessageTupleColumnValue("abcd")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/4"), LogSequenceNumber.valueOf("0/5")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/7"), 3));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("2"),
                new PgOutputMessageTupleColumnValue("defg")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/7"), LogSequenceNumber.valueOf("0/8")));

        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/A"), 4));
        add(PgOutputInsertMessage.CreateForComparison(new PgOutputMessageTuple((short) 2,
            Arrays.asList(
                new PgOutputMessageTupleColumnValue("3"),
                new PgOutputMessageTupleColumnValue("hijk")))));
        add(PgOutputCommitMessage.CreateForComparison(
            LogSequenceNumber.valueOf("0/A"), LogSequenceNumber.valueOf("0/B")));
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
    createStreamAndWaitForSnapshotTimeToPass(replConnection, "test_slot_repl_conn_all_data_types");

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
    for (PgOutputMessage res : result) {
      LOG.info("Row = {}", res);
    }

    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputTypeMessage.CreateForComparison("public", "coupon_discount_type"));
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
        replConnection, "test_slot_repl_conn_attribute_dropped");

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
    for (PgOutputMessage res : result) {
      LOG.info("Row = {}", res);
    }

    // TODO(#20726): Add comments on the choice of LSN values once we have integrated with
    // GetConsistentChanges RPC. This requires the implementation of the LSN generator to be
    // completed.
    List<PgOutputMessage> expectedResult = new ArrayList<PgOutputMessage>() {
      {
        add(PgOutputBeginMessage.CreateForComparison(LogSequenceNumber.valueOf("0/4"), 2));
        add(PgOutputRelationMessage.CreateForComparison("public", "t1", 'd',
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
}
