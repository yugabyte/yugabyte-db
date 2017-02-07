// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.fail;

import com.datastax.driver.core.exceptions.QueryValidationException;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.QueryValidationException;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.rules.ExpectedException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.MiniYBCluster;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;

import java.util.Iterator;
import java.util.Date;
import java.util.Calendar;
import java.util.GregorianCalendar;
import java.util.TimeZone;
import java.util.Map;
import java.util.HashMap;

public class TestBase {
  protected static final Logger LOG = LoggerFactory.getLogger(TestBase.class);

  protected static final String NUM_MASTERS_PROP = "NUM_MASTERS";
  protected static final int NUM_TABLET_SERVERS = 3;
  protected static final int DEFAULT_NUM_MASTERS = 3;

  // Number of masters that will be started for this test if we're starting
  // a cluster.
  protected static final int NUM_MASTERS =
    Integer.getInteger(NUM_MASTERS_PROP, DEFAULT_NUM_MASTERS);

  protected static MiniYBCluster miniCluster;

  protected static final int DEFAULT_SLEEP = 50000;

  // Long.MAX_VALUE / 1000000 is the max allowed ttl, since internally in docdb we used MonoDelta
  // to store the ttl, which uses nanoseconds.
  protected static final long MAX_TTL = Long.MAX_VALUE / 1000000;

  protected Cluster cluster;
  protected Session session;

  @BeforeClass
  public static void SetUpBeforeClass() throws Exception {
    LOG.info("Setting up before class...");

    miniCluster = new MiniYBCluster.MiniYBClusterBuilder()
                  .numMasters(NUM_MASTERS)
                  .numTservers(NUM_TABLET_SERVERS)
                  .defaultTimeoutMs(DEFAULT_SLEEP)
                  .build();

    LOG.info("Waiting for tablet servers...");
    if (!miniCluster.waitForTabletServers(NUM_TABLET_SERVERS)) {
      fail("Couldn't get " + NUM_TABLET_SERVERS + " tablet servers running, aborting");
    }
  }

  @AfterClass
  public static void TearDownAfterClass() throws Exception {
    if (miniCluster != null) {
      miniCluster.shutdown();
    }
  }

  @Before
  public void SetUpBefore() throws Exception {
    cluster = Cluster.builder()
              .addContactPointsWithPorts(miniCluster.getCQLContactPoints())
              // To sniff the CQL wire protocol using Wireshark and debug, uncomment the following
              // line to force the use of CQL V3 protocol. Wireshark does not decode V4 or higher
              // protocol yet.
              // .withProtocolVersion(com.datastax.driver.core.ProtocolVersion.V3)
             .build();
    LOG.info("Connected to cluster: " + cluster.getMetadata().getClusterName());

    session = cluster.connect();
  }

  @After
  public void TearDownAfter() throws Exception {
    session.close();
    cluster.close();
  }

  @Rule
  public ExpectedException thrown = ExpectedException.none();

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

  protected void CreateTable(String test_table) throws Exception {
     CreateTable(test_table, "varchar");
  }

  public void SetupTable(String test_table, int num_rows) throws Exception {
    CreateTable(test_table);

    LOG.info("INSERT INTO TABLE " + test_table);
    for (int idx = 0; idx < num_rows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
        test_table, idx, idx, idx+100, idx+100, idx+1000, idx+1000);
      session.execute(insert_stmt);
    }
  }

  public void TearDownTable(String test_table) throws Exception {
    LOG.info("DROP TABLE " + test_table);
    String drop_stmt = String.format("DROP TABLE %s;", test_table);
    session.execute(drop_stmt);
  }

  protected Iterator<Row> RunSelect(String tableName, String select_stmt) {
    ResultSet rs = session.execute(select_stmt);
    Iterator<Row> iter = rs.iterator();
    assertTrue(iter.hasNext());
    return iter;
  }

  protected void RunInvalidStmt(String stmt) {
    try {
      session.execute(stmt);
      fail(String.format("Statement did not fail: %s", stmt));
    } catch (QueryValidationException qv) {
      LOG.info("Expected exception", qv);
    }
  }

  // generates a comprehensive map from valid date-time inputs to corresponding Date values
  // includes both integer and string inputs --  used for Timestamp tests
  public Map<String, Date> GenerateTimestampMap() {
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
}
