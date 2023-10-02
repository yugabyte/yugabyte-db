// Copyright (c) YugaByte, Inc.
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

package org.yb.cdc.common;

import static com.google.common.base.Preconditions.*;
import static org.yb.AssertionWrappers.*;
import static org.yb.util.BuildTypeUtil.isASAN;
import static org.yb.util.BuildTypeUtil.isTSAN;

import com.google.common.net.HostAndPort;

import org.apache.commons.io.IOUtils;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import com.yugabyte.jdbc.PgArray;
import com.yugabyte.util.PGobject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.TestUtils;
import org.yb.minicluster.*;
import org.yb.util.EnvAndSysPropertyUtil;
import org.yb.util.BuildTypeUtil;
import org.yb.util.YBBackupUtil;

import java.io.File;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.sql.*;
import java.util.*;
import java.util.stream.Collectors;

// The majority of the functions in this class has been duplicated from one of the base classes
// in yb-pgsql project. It seemed necessary in order to avoid adding a complete dependency
// on the same project. This duplication will be removed in future releases.
public class CDCBaseClass extends BaseMiniClusterTest {
  private static final Logger LOG = LoggerFactory.getLogger(CDCBaseClass.class);

  protected String CDC_BATCH_SIZE_GFLAG = "cdc_snapshot_batch_size";
  protected String CDC_INTENT_SIZE_GFLAG = "cdc_max_stream_intent_records";
  protected String CDC_ENABLE_CONSISTENT_RECORDS = "cdc_enable_consistent_records";
  protected String CDC_POPULATE_SAFEPOINT_RECORD = "cdc_populate_safepoint_record";

  // Postgres settings.
  protected static final String DEFAULT_PG_DATABASE = "yugabyte";
  protected static final String DEFAULT_PG_USER = "yugabyte";
  protected static final String DEFAULT_PG_PASS = "yugabyte";

  // CQL and Redis settings, will be reset before each test via resetSettings method.
  protected boolean startCqlProxy = false;
  protected boolean startRedisProxy = false;

  protected static Connection connection;
  protected Statement statement;

  protected File pgBinDir;

  protected static boolean pgInitialized = false;

  @Override
  protected int getNumShardsPerTServer() {
    return 1;
  }

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  protected String getMasterAddresses() {
    return miniCluster.getMasterAddresses();
  }

  protected HostAndPort getTserverHostAndPort() {
    // Since we are dealing with a single tserver, so it is safe to assume that the below line
    // would return only one element, that too the one we need.
    return miniCluster.getTabletServers().keySet().iterator().next();
  }

  protected Integer getYsqlPrefetchLimit() {
    return null;
  }

  protected Integer getYsqlRequestLimit() {
    return null;
  }

  /** empty helper function */
  protected void setUp() throws Exception {
  }

  /**
   * @return flags shared between tablet server and initdb
   */
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();

    if (isTSAN() || isASAN()) {
      flagMap.put("pggate_rpc_timeout_secs", "120");
    }
    flagMap.put("start_cql_proxy", String.valueOf(startCqlProxy));
    flagMap.put("start_redis_proxy", String.valueOf(startRedisProxy));

    // Setup flag for postgres test on prefetch-limit when starting tserver.
    if (getYsqlPrefetchLimit() != null) {
      flagMap.put("ysql_prefetch_limit", getYsqlPrefetchLimit().toString());
    }

    if (getYsqlRequestLimit() != null) {
      flagMap.put("ysql_request_limit", getYsqlRequestLimit().toString());
    }

    flagMap.put("ysql_beta_features", "true");
    flagMap.put("ysql_sleep_before_retry_on_txn_conflict", "false");
    flagMap.put("ysql_max_write_restart_attempts", "2");
    flagMap.put("ysql_enable_packed_row", "false");

    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("client_read_write_timeout_ms",
      String.valueOf(BuildTypeUtil.adjustTimeout(120000)));
    flagMap.put("memory_limit_hard_bytes", String.valueOf(2L * 1024 * 1024 * 1024));
    return flagMap;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enableYsql(true);
  }

  @Before
  public void initYBBackupUtil() {
    YBBackupUtil.setMasterAddresses(masterAddresses);
    YBBackupUtil.setPostgresContactPoint(miniCluster.getPostgresContactPoints().get(0));
  }

  @Before
  public void initPostgresBefore() throws Exception {
    if (pgInitialized)
      return;

    LOG.info("Loading PostgreSQL JDBC driver");
    Class.forName("com.yugabyte.Driver");

    // Postgres bin directory.
    pgBinDir = new File(TestUtils.getBuildRootDir(), "postgres/bin");

    LOG.info("Waiting for initdb to complete on master");
    TestUtils.waitFor(
      () -> {
        IsInitDbDoneResponse initdbStatusResp = miniCluster.getClient().getIsInitDbDone();
        if (initdbStatusResp.hasError()) {
          throw new RuntimeException(
            "Could not request initdb status: " + initdbStatusResp.getServerError());
        }
        String initdbError = initdbStatusResp.getInitDbError();
        if (initdbError != null && !initdbError.isEmpty()) {
          throw new RuntimeException("initdb failed: " + initdbError);
        }
        return initdbStatusResp.isDone();
      },
      600000);
    LOG.info("initdb has completed successfully on master");

    if (connection != null) {
      LOG.info("Closing previous connection");
      connection.close();
      connection = null;
    }

    connection = getConnectionBuilder().connect();
    pgInitialized = true;
  }

  @Override
  protected void resetSettings() {
    super.resetSettings();
    startCqlProxy = false;
    startRedisProxy = false;
  }

  protected ConnectionBuilder getConnectionBuilder() {
    return new ConnectionBuilder(miniCluster);
  }

  @After
  public void cleanUpAfter() throws Exception {
    LOG.info("Cleaning up after {}", getCurrentTestMethodName());
    if (connection == null) {
      LOG.warn("No connection created, skipping cleanup");
      return;
    }

    // If root connection was closed, open a new one for cleaning.
    if (connection.isClosed()) {
      connection = getConnectionBuilder().connect();
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("RESET SESSION AUTHORIZATION");
      stmt.execute("ROLLBACK");
      stmt.execute("DISCARD TEMP");
    }

    cleanUpCustomDatabases();

    if (isClusterNeedsRecreation()) {
      pgInitialized = false;
    }
  }

  /**
   * Removes all databases excluding `postgres`, `yugabyte`, `system_platform`, `template1`, and
   * `template2`. Any lower-priority cleaners should only clean objects in one of the remaining
   * three databases, or cluster-wide objects (e.g. roles).
   */
  private void cleanUpCustomDatabases() throws Exception {
    LOG.info("Cleaning up custom databases");
    try (Statement stmt = connection.createStatement()) {
      for (int i = 0; i < 2; i++) {
        try {
          List<String> databases = getRowList(stmt,
            "SELECT datname FROM pg_database" +
              " WHERE datname <> 'template0'" +
              " AND datname <> 'template1'" +
              " AND datname <> 'postgres'" +
              " AND datname <> 'yugabyte'" +
              " AND datname <> 'system_platform'").stream().map(r -> r.getString(0))
            .collect(Collectors.toList());

          for (String database : databases) {
            LOG.info("Dropping database '{}'", database);
            stmt.execute("DROP DATABASE " + database);
          }
        } catch (Exception e) {
          if (e.toString().contains("Catalog Version Mismatch: A DDL occurred while processing")) {
            continue;
          } else {
            throw e;
          }
        }
      }
    }
  }

  @AfterClass
  public static void tearDownAfter() throws Exception {
    // Close the root connection, which is not cleaned up after each test.
    if (connection != null && !connection.isClosed()) {
      connection.close();
    }
    pgInitialized = false;
    LOG.info("Destroying mini-cluster");
    if (miniCluster != null) {
      destroyMiniCluster();
      miniCluster = null;
    }
  }

  protected static class Row implements Comparable<Row>, Cloneable {
    static Row fromResultSet(ResultSet rs) throws SQLException {
      List<Object> elems = new ArrayList<>();
      List<String> columnNames = new ArrayList<>();
      for (int i = 1; i <= rs.getMetaData().getColumnCount(); i++) {
        elems.add(rs.getObject(i));
        columnNames.add(rs.getMetaData().getColumnLabel(i));
      }
      // Pre-initialize stuff while connection is still available.
      for (Object el : elems) {
        if (el instanceof PgArray)
          ((PgArray) el).getArray();
      }
      return new Row(elems, columnNames);
    }

    List<Object> elems = new ArrayList<>();

    /**
     * List of column names, should have the same size as {@link #elems}.
     * <p>
     * Not used for equality, hash code and comparison.
     */
    List<String> columnNames = new ArrayList<>();

    Row(List<Object> elems, List<String> columnNames) {
      checkArgument(elems.size() == columnNames.size());
      this.elems = elems;
      this.columnNames = columnNames;
    }

    /** Returns a column name if available, or {@code null} otherwise. */
    String getColumnName(int index) {
      return columnNames.size() > 0 ? columnNames.get(index) : null;
    }

    String getString(int index) {
      return (String) elems.get(index);
    }

    @Override
    public boolean equals(Object obj) {
      if (obj == this) {
        return true;
      }
      if (!(obj instanceof Row)) {
        return false;
      }
      Row other = (Row)obj;
      return compareTo(other) == 0;
    }

    @Override
    public int compareTo(Row that) {
      // In our test, if selected Row has different number of columns from expected row, something
      // must be very wrong. Stop the test here.
      assertEquals("Row width mismatch between " + this + " and " + that,
        this.elems.size(), that.elems.size());
      return compare(this.elems, that.elems);
    }

    @Override
    public int hashCode() {
      return elems.hashCode();
    }

    @Override
    public String toString() {
      return toString(false /* printColumnNames */);
    }

    public String toString(boolean printColumnNames) {
      StringBuilder sb = new StringBuilder();
      sb.append("Row[");
      for (int i = 0; i < elems.size(); i++) {
        if (i > 0) sb.append(',');
        if (printColumnNames) {
          String columnNameOrNull = getColumnName(i);
          sb.append((columnNameOrNull != null ? columnNameOrNull : i) + "=");
        }
        if (elems.get(i) == null) {
          sb.append("null");
        } else {
          sb.append(elems.get(i).getClass().getName() + "::");
          sb.append(elems.get(i).toString());
        }
      }
      sb.append(']');
      return sb.toString();
    }

    @Override
    public Row clone() {
      try {
        Row clone = (Row) super.clone();
        clone.elems = new ArrayList<>(this.elems);
        clone.columnNames = new ArrayList<>(this.columnNames);
        return clone;
      } catch (CloneNotSupportedException ex) {
        // Not possible
        throw new RuntimeException(ex);
      }
    }

    /**
     * Compare two objects if possible. Is able to compare:
     * <ul>
     * <li>Primitives
     * <li>Comparables (including {@code String}) - but cannot handle the case of two unrelated
     * Comparables
     * <li>{@code PGobject}s wrapping {@code Comparable}s
     * <li>Arrays, {@code PgArray}s or lists of the above types
     * </ul>
     */
    @SuppressWarnings({ "unchecked", "rawtypes" })
    private static int compare(Object o1, Object o2) {
      if (o1 == null || o2 == null) {
        if (o1 != o2) {
          return o1 == null ? -1 : 1;
        }
        return 0;
      } else {
        Object promoted1 = promoteType(o1);
        Object promoted2 = promoteType(o2);
        if (promoted1 instanceof Long && promoted2 instanceof Long) {
          return ((Long) promoted1).compareTo((Long) promoted2);
        } else if (promoted1 instanceof Double && promoted2 instanceof Double) {
          return ((Double) promoted1).compareTo((Double) promoted2);
        } else if (promoted1 instanceof Number && promoted2 instanceof Number) {
          return Double.compare(
            ((Number) promoted1).doubleValue(),
            ((Number) promoted2).doubleValue());
        } else if (promoted1 instanceof Comparable && promoted2 instanceof Comparable) {
          // This is unsafe but we dont expect arbitrary types here.
          return ((Comparable) promoted1).compareTo((Comparable) promoted2);
        } else if (promoted1 instanceof List && promoted2 instanceof List) {
          List list1 = (List) promoted1;
          List list2 = (List) promoted2;
          if (list1.size() != list2.size()) {
            return Integer.compare(list1.size(), list2.size());
          }
          for (int i = 0; i < list1.size(); ++i) {
            int comparisonResult = compare(list1.get(i), list2.get(i));
            if (comparisonResult != 0) {
              return comparisonResult;
            }
          }
          return 0;
        } else {
          throw new IllegalArgumentException("Cannot compare "
            + o1 + " (of class " + o1.getClass().getCanonicalName() + ") with "
            + o2 + " (of class " + o1.getClass().getCanonicalName() + ")");
        }
      }
    }

    /** Converts the value to a widest one of the same type for comparison */
    @SuppressWarnings({ "rawtypes", "unchecked" })
    private static Object promoteType(Object v) {
      if (v instanceof Byte || v instanceof Short || v instanceof Integer) {
        return ((Number)v).longValue();
      } else if (v instanceof Float) {
        return ((Float)v).doubleValue();
      } else if (v instanceof Comparable) {
        return v;
      } else if (v instanceof List) {
        return v;
      } else if (v instanceof PGobject) {
        return promoteType(((PGobject) v).getValue()); // For PG_LSN type.
      } else if (v instanceof PgArray) {
        try {
          return promoteType(((PgArray) v).getArray());
        } catch (SQLException ex) {
          throw new RuntimeException("SQL exception during type promotion", ex);
        }
      } else if (v.getClass().isArray()) {
        List list = new ArrayList<>();
        // Unfortunately there's no easy way to automate that, we have to enumerate all array types
        // explicitly.
        if (v instanceof byte[]) {
          for (byte ve : (byte[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof short[]) {
          for (short ve : (short[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof int[]) {
          for (int ve : (int[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof long[]) {
          for (long ve : (long[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof float[]) {
          for (float ve : (float[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof double[]) {
          for (double ve : (double[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof boolean[]) {
          for (boolean ve : (boolean[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof char[]) {
          for (char ve : (char[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof Object[]) {
          for (Object ve : (Object[]) v) {
            list.add(promoteType(ve));
          }
        }
        return list;
      } else {
        throw new IllegalArgumentException(v + " (of class " + v.getClass().getSimpleName() + ")"
          + " cannot be promoted for comparison!");
      }
    }
  }

  protected List<Row> getRowList(Statement stmt, String query) throws SQLException {
    try (ResultSet rs = stmt.executeQuery(query)) {
      return getRowList(rs);
    }
  }

  protected static List<Row> getRowList(ResultSet rs) throws SQLException {
    List<Row> rows = new ArrayList<>();
    while (rs.next()) {
      rows.add(Row.fromResultSet(rs));
    }
    return rows;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    // initdb takes a really long time on macOS in debug mode.
    return 1200;
  }

  /** Run a process, returning output lines. */
  protected List<String> runProcess(String... args) throws Exception {
    return runProcess(new ProcessBuilder(args));
  }

  /** Run a process, returning output lines. */
  protected List<String> runProcess(ProcessBuilder procBuilder) throws Exception {
    Process proc = procBuilder.start();
    int code = proc.waitFor();
    if (code != 0) {
      String err = IOUtils.toString(proc.getErrorStream(), StandardCharsets.UTF_8);
      fail("Process exited with code " + code + ", message: <" + err.trim() + ">");
    }
    String output = IOUtils.toString(proc.getInputStream(), StandardCharsets.UTF_8);
    return Arrays.asList(output.split("\n"));
  }

  protected void setServerFlag(HostAndPort server, String flag, String value) throws Exception {
    runProcess(TestUtils.findBinary("yb-ts-cli"),
      "--server_address",
      server.toString(),
      "set_flag",
      "-force",
      flag,
      value);
  }

  protected void splitTablet(String masterAddresses, String tabletId) throws Exception {
    runProcess(TestUtils.findBinary("yb-admin"),
      "--master_addresses",
      masterAddresses,
      "split_tablet",
      tabletId);
  }

  /**
   * Create a DB Stream ID using yb-admin tool
   * @param masterAddresses
   * @param namespaceName
   * @return DB Stream ID
   * @throws Exception
   */
  protected String createDBStreamUsingYbAdmin(String masterAddresses,
                                              String namespaceName) throws Exception {
    // the command only returns one line so it is safe to fetch the line at index 0
    String outputLine = runProcess(TestUtils.findBinary("yb-admin"),
      "-master_addresses",
      masterAddresses,
      "create_change_data_stream",
      "ysql."+namespaceName).get(0);

    if (outputLine.toLowerCase(Locale.ROOT).contains("error")) {
      throw new RuntimeException("Error while creating DB Stream");
    }

    // getting the stream id from this line
    String[] splitRes = outputLine.split(":");

    // the last element after splitting will be the stream ID
    return splitRes[splitRes.length - 1].trim();
  }

  /**
   * Delete a created DB stream ID using the yb-admin tool
   * @param masterAddresses
   * @param dbStreamId
   * @return The deleted stream ID
   * @throws Exception
   */
  protected String deleteDBStreamUsingYbAdmin(String masterAddresses,
                                              String dbStreamId) throws Exception {
    // the output contains one line only
    String outputLine = runProcess(TestUtils.findBinary("yb-admin"),
      "-master_addresses",
      masterAddresses,
      "delete_change_data_stream",
      dbStreamId).get(0);

    if (outputLine.toLowerCase(Locale.ROOT).contains("error")) {
      throw new RuntimeException("Error while deleting DB Stream");
    }

    // getting the stream ID from this line, the stream ID will be the last element after the split
    String[] splitRes = outputLine.split(":");

    return splitRes[splitRes.length - 1].trim();
  }

  public static class ConnectionBuilder implements Cloneable {
    private static final int MAX_CONNECTION_ATTEMPTS = 15;
    private static final int INITIAL_CONNECTION_DELAY_MS = 500;

    private final MiniYBCluster miniCluster;

    private int tserverIndex = 0;
    private String database = DEFAULT_PG_DATABASE;
    private String user = DEFAULT_PG_USER;
    private String password = null;
    private String preferQueryMode = null;
    private String sslmode = null;
    private String sslcert = null;
    private String sslkey = null;
    private String sslrootcert = null;

    ConnectionBuilder(MiniYBCluster miniCluster) {
      this.miniCluster = checkNotNull(miniCluster);
    }

    ConnectionBuilder withTServer(int tserverIndex) {
      ConnectionBuilder copy = clone();
      copy.tserverIndex = tserverIndex;
      return copy;
    }

    ConnectionBuilder withDatabase(String database) {
      ConnectionBuilder copy = clone();
      copy.database = database;
      return copy;
    }

    ConnectionBuilder withUser(String user) {
      ConnectionBuilder copy = clone();
      copy.user = user;
      return copy;
    }

    ConnectionBuilder withPassword(String password) {
      ConnectionBuilder copy = clone();
      copy.password = password;
      return copy;
    }


    ConnectionBuilder withPreferQueryMode(String preferQueryMode) {
      ConnectionBuilder copy = clone();
      copy.preferQueryMode = preferQueryMode;
      return copy;
    }

    ConnectionBuilder withSslMode(String sslmode) {
      ConnectionBuilder copy = clone();
      copy.sslmode = sslmode;
      return copy;
    }

    ConnectionBuilder withSslCert(String sslcert) {
      ConnectionBuilder copy = clone();
      copy.sslcert = sslcert;
      return copy;
    }

    ConnectionBuilder withSslKey(String sslkey) {
      ConnectionBuilder copy = clone();
      copy.sslkey = sslkey;
      return copy;
    }

    ConnectionBuilder withSslRootCert(String sslrootcert) {
      ConnectionBuilder copy = clone();
      copy.sslrootcert = sslrootcert;
      return copy;
    }

    @Override
    protected ConnectionBuilder clone() {
      try {
        return (ConnectionBuilder) super.clone();
      } catch (CloneNotSupportedException ex) {
        throw new RuntimeException("This can't happen, but to keep compiler happy", ex);
      }
    }

    Connection connect() throws Exception {
      final InetSocketAddress postgresAddress = miniCluster.getPostgresContactPoints()
        .get(tserverIndex);
      String url = String.format(
        "jdbc:yugabytedb://%s:%d/%s",
        postgresAddress.getHostName(),
        postgresAddress.getPort(),
        database
      );

      Properties props = new Properties();
      props.setProperty("user", user);
      if (password != null) {
        props.setProperty("password", password);
      }
      if (preferQueryMode != null) {
        props.setProperty("preferQueryMode", preferQueryMode);
      }
      if (sslmode != null) {
        props.setProperty("sslmode", sslmode);
      }
      if (sslcert != null) {
        props.setProperty("sslcert", sslcert);
      }
      if (sslkey != null) {
        props.setProperty("sslkey", sslkey);
      }
      if (sslrootcert != null) {
        props.setProperty("sslrootcert", sslrootcert);
      }
      if (EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_PG_JDBC_TRACE_LOGGING")) {
        props.setProperty("loggerLevel", "TRACE");
      }

      int delayMs = INITIAL_CONNECTION_DELAY_MS;
      for (int attempt = 1; attempt <= MAX_CONNECTION_ATTEMPTS; ++attempt) {
        Connection connection = null;
        try {
          connection = checkNotNull(DriverManager.getConnection(url, props));
          connection.setAutoCommit(true);

          return connection;
        } catch (SQLException sqlEx) {
          // Close the connection now if we opened it, instead of waiting until the end of the test.
          if (connection != null) {
            try {
              connection.close();
            } catch (SQLException closingError) {
              LOG.error("Failure to close connection during failure cleanup before a retry:",
                closingError);
              LOG.error("When handling this exception when opening/setting up connection:", sqlEx);
            }
          }

          boolean retry = false;

          if (attempt < MAX_CONNECTION_ATTEMPTS) {
            if (sqlEx.getMessage().contains("FATAL: the database system is starting up")
              || sqlEx.getMessage().contains("refused. Check that the hostname and port are " +
              "correct and that the postmaster is accepting")) {
              retry = true;

              LOG.info("Postgres is still starting up, waiting for " + delayMs + " ms. " +
                "Got message: " + sqlEx.getMessage());
            } else if (sqlEx.getMessage().contains("the database system is in recovery mode")) {
              retry = true;

              LOG.info("Postgres is in recovery mode, waiting for " + delayMs + " ms. " +
                "Got message: " + sqlEx.getMessage());
            }
          }

          if (retry) {
            Thread.sleep(delayMs);
            delayMs = Math.min(delayMs + 500, 10000);
          } else {
            LOG.error("Exception while trying to create connection (after " + attempt +
              " attempts): " + sqlEx.getMessage());
            throw sqlEx;
          }
        }
      }
      throw new IllegalStateException("Should not be able to reach here");
    }
  }
}
