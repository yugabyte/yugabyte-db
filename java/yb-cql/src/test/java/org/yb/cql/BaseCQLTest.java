// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.datastax.driver.core.exceptions.QueryValidationException;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.BaseMiniClusterTest;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.SocketOptions;
import org.yb.master.Master;

import java.nio.ByteBuffer;
import java.util.*;

public class BaseCQLTest extends BaseMiniClusterTest {
  protected static final Logger LOG = LoggerFactory.getLogger(BaseCQLTest.class);

  // Long.MAX_VALUE / 1000000000 is the max allowed ttl, since internally in docdb we use MonoDelta
  // to store the ttl, which uses nanoseconds.
  protected static final long MAX_TTL_SEC = Long.MAX_VALUE / 1000000000;

  protected static final String DEFAULT_KEYSPACE = "default_keyspace";

  protected static final String DEFAULT_TEST_KEYSPACE = "cql_test_keyspace";

  protected Cluster cluster;
  protected Session session;

  /** These keyspaces will be dropped in after the current test method (in the @After handler). */
  protected Set<String> keyspacesToDrop = new TreeSet<>();

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    LOG.info("BaseCQLTest.setUpBeforeClass is running");

    // Disable extended peer check, to ensure "SELECT * FROM system.peers" works without
    // all columns.
    System.setProperty("com.datastax.driver.EXTENDED_PEER_CHECK", "false");
  }

  @Before
  public void setUpCqlClient() throws Exception {
    LOG.info("BaseCQLTest.setUpCqlClient is running");
    // Set a long timeout for CQL queries since build servers might be really slow (especially Mac
    // Mini).
    SocketOptions socketOptions = new SocketOptions();
    socketOptions.setReadTimeoutMillis(60 * 1000);
    cluster = Cluster.builder()
              .addContactPointsWithPorts(miniCluster.getCQLContactPoints())
              // To sniff the CQL wire protocol using Wireshark and debug, uncomment the following
              // line to force the use of CQL V3 protocol. Wireshark does not decode V4 or higher
              // protocol yet.
              // .withProtocolVersion(com.datastax.driver.core.ProtocolVersion.V3)
             .withSocketOptions(socketOptions)
             .build();
    LOG.info("Connected to cluster: " + cluster.getMetadata().getClusterName());

    session = cluster.connect();

    // Create and use test keyspace to be able using short table names later.
    createKeyspaceIfNotExists(DEFAULT_TEST_KEYSPACE);
    useKeyspace(DEFAULT_TEST_KEYSPACE);
  }

  @After
  public void tearDownAfter() throws Exception {
    LOG.info("BaseCQLTest.tearDownAfter is running: " +
        "dropping tables and closing CQL session/client");

    // Clean up all tables before end of each test to lower high-water-mark disk usage.
    dropTables();

    // Also delete all keyspaces, because we may
    dropKeyspaces();

    if (session != null) {
      session.close();
    }
    if (cluster != null) {
      cluster.close();
    }
    LOG.info("BaseCQLTest.tearDownAfter is running: " +
        "finished dropping tables and closing CQL session/client");
    afterBaseCQLTestTearDown();
  }

  protected void afterBaseCQLTestTearDown() throws Exception {
  }

  protected void CreateTable(String test_table, String column_type) throws Exception {
    LOG.info("CREATE TABLE " + test_table);
    String create_stmt = String.format("CREATE TABLE %s " +
                    " (h1 int, h2 %2$s, " +
                    " r1 int, r2 %2$s, " +
                    " v1 int, v2 %2$s, " +
                    " primary key((h1, h2), r1, r2));",
            test_table, column_type);
    session.execute(create_stmt);
  }

  protected void CreateTable(String tableName) throws Exception {
     CreateTable(tableName, "varchar");
  }

  public void setupTable(String tableName, int numRows) throws Exception {
    CreateTable(tableName);

    LOG.info("INSERT INTO TABLE " + tableName);
    for (int idx = 0; idx < numRows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
        tableName, idx, idx, idx+100, idx+100, idx+1000, idx+1000);
      session.execute(insert_stmt);
    }
    LOG.info("INSERT INTO TABLE " + tableName + " FINISHED");
  }

  protected void DropTable(String tableName) throws Exception {
    String drop_stmt = String.format("DROP TABLE %s;", tableName);
    session.execute(drop_stmt);
  }

  protected void dropTables() throws Exception {
    if (miniCluster == null) {
      return;
    }
    for (Master.ListTablesResponsePB.TableInfo tableInfo :
        miniCluster.getClient().getTablesList().getTableInfoList()) {
      // Drop all non-system tables.
      String namespaceName = tableInfo.getNamespace().getName();
      if (!namespaceName.equals("system") &&
          !namespaceName.equals("system_auth") &&
          !namespaceName.equals("system_distributed") &&
          !namespaceName.equals("system_traces") &&
          !namespaceName.equals("system_schema")) {
        if (namespaceName.equals(DEFAULT_KEYSPACE)) {
          // Don't need namespace name for default namespace.
          DropTable(tableInfo.getName());
        } else {
          DropTable(namespaceName + "." + tableInfo.getName());
        }
      }
    }
  }

  protected void dropKeyspaces() throws Exception {
    if (keyspacesToDrop.isEmpty()) {
      LOG.info("No keyspaces to drop after the test.");
    }

    // Copy the set of keyspaces to drop to avoid concurrent modification exception, because
    // dropKeyspace will also remove them from the keyspacesToDrop.
    final Set<String> keyspacesToDropCopy = new TreeSet<>();
    keyspacesToDropCopy.addAll(keyspacesToDrop);
    for (String keyspaceName : keyspacesToDropCopy) {
      dropKeyspace(keyspaceName);
    }
  }

  protected Iterator<Row> runSelect(String select_stmt) {
    ResultSet rs = session.execute(select_stmt);
    Iterator<Row> iter = rs.iterator();
    assertTrue(iter.hasNext());
    return iter;
  }

  protected void runInvalidStmt(String stmt) {
    try {
      session.execute(stmt);
      fail(String.format("Statement did not fail: %s", stmt));
    } catch (QueryValidationException qv) {
      LOG.info("Expected exception", qv);
    }
  }

  // generates a comprehensive map from valid date-time inputs to corresponding Date values
  // includes both integer and string inputs --  used for Timestamp tests
  public Map<String, Date> generateTimestampMap() {
    Map<String, Date> ts_values = new HashMap();
    Calendar cal = new GregorianCalendar();

    // adding some Integer input values
    cal.setTimeInMillis(631238400000L);
    ts_values.put("631238400000", cal.getTime());
    cal.setTimeInMillis(631238434123L);
    ts_values.put("631238434123", cal.getTime());
    cal.setTimeInMillis(631238445000L);
    ts_values.put("631238445000", cal.getTime());

    // generating String inputs as combinations valid components (date/time/frac_seconds/timezone)
    int nr_entries = 3;
    String[] dates = {"'1992-06-04", "'1992-6-4", "'1992-06-4"};
    String[] times_no_sec = {"12:30", "15:30", "9:00"};
    String[] times = {"12:30:45", "15:30:45", "9:00:45"};
    String[] times_frac = {"12:30:45.1", "15:30:45.10", "9:00:45.100"};
    // timezones correspond one-to-one with times
    //   -- so that the UTC-normalized time is the same
    String[] timezones = {" UTC'", "+03:00'", " UTC-03:30'"};
    for (String date : dates) {
      cal.setTimeZone(TimeZone.getTimeZone("GMT")); // resetting
      cal.setTimeInMillis(0); // resetting
      cal.set(1992, 5, 4); // Java Date month value starts at 0 not 1
      ts_values.put(date + " UTC'", cal.getTime());

      cal.set(Calendar.HOUR_OF_DAY, 12);
      cal.set(Calendar.MINUTE, 30);
      for (int i = 0; i < nr_entries; i++) {
        String time = times_no_sec[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
      cal.set(Calendar.SECOND, 45);
      for (int i = 0; i < nr_entries; i++) {
        String time = times[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
      cal.set(Calendar.MILLISECOND, 100);
      for (int i = 0; i < nr_entries; i++) {
        String time = times_frac[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
    }
    return ts_values;
  }

  protected void assertNoRow(String select_stmt) {
    ResultSet rs = session.execute(select_stmt);
    Iterator<Row> iter = rs.iterator();
    assertFalse(iter.hasNext());
  }

  protected void assertQuery(String stmt, String expectedResult) {
    ResultSet rs = session.execute(stmt);
    String actualResult = "";
    for (Row row : rs) {
      actualResult += row.toString();
    }
    assertEquals(expectedResult, actualResult);
  }

  // blob type utils
  String makeBlobString(ByteBuffer buf) {
    StringBuilder sb = new StringBuilder();
    char[] text_values = "0123456789abcdef".toCharArray();

    sb.append("0x");
    while (buf.hasRemaining()) {
      byte b = buf.get();
      sb.append(text_values[(b & 0xFF) >>> 4]);
      sb.append(text_values[b & 0x0F]);
    }
    return sb.toString();
  }

  ByteBuffer makeByteBuffer(long input) {
    ByteBuffer buffer = ByteBuffer.allocate(Long.BYTES);
    buffer.putLong(input);
    buffer.rewind();
    return buffer;
  }

  public ResultSet execute(String statement) throws Exception {
    LOG.info("EXEC CQL: " + statement);
    final ResultSet result = session.execute(statement);
    LOG.info("EXEC CQL FINISHED: " + statement);
    return result;
  }

  public void createKeyspace(String keyspaceName) throws Exception {
    String createKeyspaceStmt = "CREATE KEYSPACE \"" + keyspaceName + "\";";
    execute(createKeyspaceStmt);
    keyspacesToDrop.add(keyspaceName);
  }

  public void createKeyspaceIfNotExists(String keyspaceName) throws Exception {
    String createKeyspaceStmt = "CREATE KEYSPACE IF NOT EXISTS \"" + keyspaceName + "\";";
    execute(createKeyspaceStmt);
    keyspacesToDrop.add(keyspaceName);
  }

  public void useKeyspace(String keyspaceName) throws Exception {
    String useKeyspaceStmt = "USE \"" + keyspaceName + "\";";
    execute(useKeyspaceStmt);
  }

  public void dropKeyspace(String keyspaceName) throws Exception {
    String deleteKeyspaceStmt = "DROP KEYSPACE \"" + keyspaceName + "\";";
    execute(deleteKeyspaceStmt);
    keyspacesToDrop.remove(keyspaceName);
  }

}
