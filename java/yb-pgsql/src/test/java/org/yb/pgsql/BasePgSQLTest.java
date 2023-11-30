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
package org.yb.pgsql;

import static com.google.common.base.Preconditions.checkArgument;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertLessThan;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;
import static org.yb.util.BuildTypeUtil.isASAN;
import static org.yb.util.BuildTypeUtil.isTSAN;

import java.io.File;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLConnection;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.Scanner;
import java.util.Set;
import java.util.TreeMap;
import java.util.concurrent.ConcurrentSkipListSet;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.TestUtils;
import org.yb.master.MasterDdlOuterClass;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.Metrics.YSQLStat;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.RocksDBMetrics;
import org.yb.minicluster.YsqlSnapshotVersion;
import org.yb.util.BuildTypeUtil;
import org.yb.util.MiscUtil.ThrowingCallable;
import org.yb.util.SystemUtil;
import org.yb.util.ThrowingRunnable;
import org.yb.util.YBBackupException;
import org.yb.util.YBBackupUtil;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.yugabyte.core.TransactionState;
import com.yugabyte.jdbc.PgArray;
import com.yugabyte.jdbc.PgConnection;
import com.yugabyte.util.PGobject;
import com.yugabyte.util.PSQLException;

public class BasePgSQLTest extends BaseMiniClusterTest {
  private static final Logger LOG = LoggerFactory.getLogger(BasePgSQLTest.class);

  /** Corresponds to the first OID used for YB system catalog objects. */
  protected final long FIRST_YB_OID = 8000;

  /** Matches Postgres' FirstBootstrapObjectId */
  protected final long FIRST_BOOTSTRAP_OID = 10000;

  /** Matches Postgres' FirstNormalObjectId */
  protected final long FIRST_NORMAL_OID = 16384;

  // Postgres settings.
  // TODO(janand) GH #17899 Deduplicate DEFAULT_PG_DATABASE and TEST_PG_USER (present in
  // BasePgSQLTest and ConnectionBuilder)
  // NOTE: Values for DEFAULT_PG_DATABASE and TEST_PG_USER should be same in both
  // org.yb.pgsql.ConnectionBuilder and org.yb.pgsql.BasePgSQLTest
  protected static final String DEFAULT_PG_DATABASE = ConnectionBuilder.DEFAULT_PG_DATABASE;
  protected static final String DEFAULT_PG_USER = "yugabyte";
  protected static final String DEFAULT_PG_PASS = "yugabyte";
  protected static final String TEST_PG_USER = ConnectionBuilder.TEST_PG_USER;
  protected static final String TEST_PG_PASS = "pass";

  // Non-standard PSQL states defined in yb_pg_errcodes.h
  protected static final String SERIALIZATION_FAILURE_PSQL_STATE = "40001";
  protected static final String SNAPSHOT_TOO_OLD_PSQL_STATE = "72000";
  protected static final String DEADLOCK_DETECTED_PSQL_STATE = "40P01";

  // Postgres flags.
  private static final String MASTERS_FLAG = "FLAGS_pggate_master_addresses";
  private static final String YB_ENABLED_IN_PG_ENV_VAR_NAME = "YB_ENABLED_IN_POSTGRES";

  // Metric names.
  protected static final String METRIC_PREFIX = "handler_latency_yb_ysqlserver_SQLProcessor_";
  protected static final String SELECT_STMT_METRIC = METRIC_PREFIX + "SelectStmt";
  protected static final String INSERT_STMT_METRIC = METRIC_PREFIX + "InsertStmt";
  protected static final String DELETE_STMT_METRIC = METRIC_PREFIX + "DeleteStmt";
  protected static final String UPDATE_STMT_METRIC = METRIC_PREFIX + "UpdateStmt";
  protected static final String BEGIN_STMT_METRIC = METRIC_PREFIX + "BeginStmt";
  protected static final String COMMIT_STMT_METRIC = METRIC_PREFIX + "CommitStmt";
  protected static final String ROLLBACK_STMT_METRIC = METRIC_PREFIX + "RollbackStmt";
  protected static final String OTHER_STMT_METRIC = METRIC_PREFIX + "OtherStmts";
  protected static final String SINGLE_SHARD_TRANSACTIONS_METRIC_DEPRECATED = METRIC_PREFIX
      + "Single_Shard_Transactions";
  protected static final String SINGLE_SHARD_TRANSACTIONS_METRIC =
      METRIC_PREFIX + "SingleShardTransactions";
  protected static final String TRANSACTIONS_METRIC = METRIC_PREFIX + "Transactions";
  protected static final String AGGREGATE_PUSHDOWNS_METRIC = METRIC_PREFIX + "AggregatePushdowns";
  protected static final String CATALOG_CACHE_MISSES_METRICS = METRIC_PREFIX + "CatalogCacheMisses";

  // CQL and Redis settings, will be reset before each test via resetSettings method.
  protected boolean startCqlProxy = false;
  protected boolean startRedisProxy = false;

  protected static Connection connection;

  protected File pgBinDir;

  protected static final int DEFAULT_STATEMENT_TIMEOUT_MS = 30000;

  protected static ConcurrentSkipListSet<Integer> stuckBackendPidsConcMap =
      new ConcurrentSkipListSet<>();

  protected static boolean pgInitialized = false;

  public void runPgRegressTest(
      File inputDir, String schedule, long maxRuntimeMillis, File executable) throws Exception {
    final int tserverIndex = 0;
    PgRegressRunner pgRegress = new PgRegressRunner(inputDir, schedule, maxRuntimeMillis);
    ProcessBuilder procBuilder = new PgRegressBuilder(executable)
        .setDirs(inputDir, pgRegress.outputDir())
        .setSchedule(schedule)
        .setHost(getPgHost(tserverIndex))
        .setPort(getPgPort(tserverIndex))
        .setUser(DEFAULT_PG_USER)
        .setDatabase("yugabyte")
        .setEnvVars(getPgRegressEnvVars())
        .getProcessBuilder();
    pgRegress.run(procBuilder);
  }

  public void runPgRegressTest(File inputDir, String schedule) throws Exception {
    runPgRegressTest(
        inputDir, schedule, 0 /* maxRuntimeMillis */,
        PgRegressBuilder.PG_REGRESS_EXECUTABLE);
  }

  public void runPgRegressTest(String schedule, long maxRuntimeMillis) throws Exception {
    runPgRegressTest(
        PgRegressBuilder.PG_REGRESS_DIR /* inputDir */, schedule, maxRuntimeMillis,
        PgRegressBuilder.PG_REGRESS_EXECUTABLE);
  }

  public void runPgRegressTest(String schedule) throws Exception {
    runPgRegressTest(schedule, 0 /* maxRuntimeMillis */);
  }

  public static void perfAssertLessThan(double time1, double time2) {
    if (TestUtils.isReleaseBuild()) {
      assertLessThan(time1, time2);
    }
  }

  public static void perfAssertEquals(double time1, double time2) {
    if (TestUtils.isReleaseBuild()) {
      assertLessThan(time1, time2 * 1.3);
      assertLessThan(time1 * 1.3, time2);
    }
  }

  protected static int getPerfMaxRuntime(int releaseRuntime,
                                         int debugRuntime,
                                         int asanRuntime,
                                         int tsanRuntime,
                                         int macRuntime) {
    if (TestUtils.isReleaseBuild()) {
      return releaseRuntime;
    } else if (SystemUtil.IS_LINUX) {
      if (BuildTypeUtil.isASAN()) {
        return asanRuntime;
      } else if (BuildTypeUtil.isTSAN()) {
        return tsanRuntime;
      } else {
        // Linux debug builds.
        return debugRuntime;
      }
    } else {
      // We get a lot of timeouts in macOS debug builds.
      return macRuntime;
    }
  }

  protected Integer getYsqlPrefetchLimit() {
    return null;
  }

  protected Integer getYsqlRequestLimit() {
    return null;
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
    flagMap.put("ysql_enable_reindex", "true");

    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("client_read_write_timeout_ms",
        String.valueOf(BuildTypeUtil.adjustTimeout(120000)));
    flagMap.put("memory_limit_hard_bytes", String.valueOf(2L * 1024 * 1024 * 1024));
    flagMap.put("TEST_assert_no_future_catalog_version", "true");
    return flagMap;
  }

  @Override
  protected int getNumShardsPerTServer() {
    return 1;
  }

  @Override
  protected int getReplicationFactor() {
    return 3;
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

    connection = createTestRole();
    pgInitialized = true;
  }

  protected Connection createTestRole() throws Exception {
    try (Connection initialConnection = getConnectionBuilder().withUser(DEFAULT_PG_USER).connect();
         Statement statement = initialConnection.createStatement()) {
        statement.execute(
            String.format("CREATE ROLE %s SUPERUSER CREATEROLE CREATEDB BYPASSRLS LOGIN ",
                          TEST_PG_USER));
    }

    return getConnectionBuilder().connect();
  }

  public void restartClusterWithFlags(
      Map<String, String> additionalMasterFlags,
      Map<String, String> additionalTserverFlags) throws Exception {
    destroyMiniCluster();

    createMiniCluster(additionalMasterFlags, additionalTserverFlags);
    pgInitialized = false;
    initPostgresBefore();
  }

  public void restartClusterWithClusterBuilder(
    Consumer<MiniYBClusterBuilder> customize) throws Exception {
    destroyMiniCluster();

    createMiniCluster(customize);
    pgInitialized = false;
    initPostgresBefore();
  }

  public void restartClusterWithFlagsAndEnv(
      Map<String, String> additionalMasterFlags,
      Map<String, String> additionalTserverFlags,
      Map<String, String> additionalEnvironmentVars) throws Exception {
    destroyMiniCluster();

    createMiniCluster(additionalMasterFlags, additionalTserverFlags, additionalEnvironmentVars);
    pgInitialized = false;
    initPostgresBefore();
  }

  public void restartCluster() throws Exception {
    restartClusterWithFlags(
      Collections.<String, String>emptyMap(),
      Collections.<String, String>emptyMap());
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

  public String getPgHost(int tserverIndex) {
    return miniCluster.getPostgresContactPoints().get(tserverIndex).getHostName();
  }

  public int getPgPort(int tserverIndex) {
    return miniCluster.getPostgresContactPoints().get(tserverIndex).getPort();
  }

  protected Map<String, String> getPgRegressEnvVars() {
    Map<String, String> pgRegressEnvVars = new TreeMap<>();
    pgRegressEnvVars.put(MASTERS_FLAG, masterAddresses);
    pgRegressEnvVars.put(YB_ENABLED_IN_PG_ENV_VAR_NAME, "1");

    for (Map.Entry<String, String> entry : System.getenv().entrySet()) {
      String envVarName = entry.getKey();
      if (envVarName.startsWith("postgres_FLAGS_")) {
        String downstreamEnvVarName = envVarName.substring(9);
        LOG.info("Found env var " + envVarName + ", setting " + downstreamEnvVarName + " for " +
                 "pg_regress to " + entry.getValue());
        pgRegressEnvVars.put(downstreamEnvVarName, entry.getValue());
      }
    }

    // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
    pgRegressEnvVars.put("YB_PG_FALLBACK_SYSTEM_USER_NAME", "yugabyte");

    return pgRegressEnvVars;
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

      // TODO(dmitry): Workaround for #1721, remove after fix.
      stmt.execute("ROLLBACK");
      stmt.execute("DISCARD TEMP");
    }

    cleanUpCustomDatabases();

    cleanUpCustomEntities();

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

  /** Drop entities owned by non-system roles, and drop custom roles. */
  private void cleanUpCustomEntities() throws Exception {
    LOG.info("Cleaning up roles");
    List<String> persistentUsers = Arrays.asList(DEFAULT_PG_USER, TEST_PG_USER);
    try (Statement stmt = connection.createStatement()) {
      for (int i = 0; i < 2; i++) {
        try {
          List<String> roles = getRowList(stmt, "SELECT rolname FROM pg_roles"
              + " WHERE rolname <> 'postgres'"
              + " AND rolname NOT LIKE 'pg_%'"
              + " AND rolname NOT LIKE 'yb_%'").stream()
                  .map(r -> r.getString(0))
                  .collect(Collectors.toList());

          for (String role : roles) {
            boolean isPersistent = persistentUsers.contains(role);
            LOG.info("Cleaning up role {} (persistent? {})", role, isPersistent);
            stmt.execute("DROP OWNED BY " + role + " CASCADE");
          }

          // Documentation for DROP OWNED BY explicitly states that databases and tablespaces
          // are not removed, so we do this ourself.
          // Ref: https://www.postgresql.org/docs/11/sql-drop-owned.html
          List<String> tablespaces = getRowList(stmt, "SELECT spcname FROM pg_tablespace"
              + " WHERE spcowner NOT IN ("
              + "   SELECT oid FROM pg_roles "
              + "   WHERE rolname = 'postgres' OR rolname LIKE 'pg_%' OR rolname LIKE 'yb_%'"
              + ")").stream()
                  .map(r -> r.getString(0))
                  .collect(Collectors.toList());

          for (String tablespace : tablespaces) {
            stmt.execute("DROP TABLESPACE " + tablespace);
          }

          for (String role : roles) {
            boolean isPersistent = persistentUsers.contains(role);
            if (!isPersistent) {
              LOG.info("Dropping role {}", role);
              stmt.execute("DROP ROLE " + role);
            }
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

  protected void recreateWithYsqlVersion(YsqlSnapshotVersion version) throws Exception {
    destroyMiniCluster();
    pgInitialized = false;
    markClusterNeedsRecreation();
    createMiniCluster((builder) -> {
      builder.ysqlSnapshotVersion(version);
    });
    initPostgresBefore();
  }

  /**
   * Commit the current transaction on the given connection, catch and report the exception.
   * @param conn connection to use
   * @param extraMsg an extra part of the error message
   * @return whether commit succeeded
   */
  protected static boolean commitAndCatchException(Connection conn, String extraMsg) {
    extraMsg = extraMsg.trim();
    if (!extraMsg.isEmpty()) {
      extraMsg = " (" + extraMsg + ")";
    }
    try {
      conn.commit();
      return true;
    } catch (SQLException ex) {
      // TODO: validate the exception message.
      LOG.info("Error during commit" + extraMsg + ": " + ex.getMessage());
      return false;
    }
  }

  protected static PgConnection toPgConnection(Connection connection) {
    return (PgConnection) connection;
  }

  protected static TransactionState getPgTxnState(Connection connection) {
    return toPgConnection(connection).getTransactionState();
  }

  protected static int getPgBackendPid(Connection connection) {
    return toPgConnection(connection).getBackendPID();
  }

  protected static class AggregatedValue {
    long count;
    double value;
    long rows;
  }

  protected void resetStatementStat() throws Exception {
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      URL url = new URL(String.format("http://%s:%d/statements-reset",
                                      ts.getLocalhostIP(),
                                      ts.getPgsqlWebPort()));
      Scanner scanner = new Scanner(url.openConnection().getInputStream());
      scanner.close();
    }
  }

  protected AggregatedValue getStatementStat(String statName) throws Exception {
    AggregatedValue value = new AggregatedValue();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      URL url = new URL(String.format("http://%s:%d/statements",
                                      ts.getLocalhostIP(),
                                      ts.getPgsqlWebPort()));
      Scanner scanner = new Scanner(url.openConnection().getInputStream());
      JsonElement tree = JsonParser.parseString(scanner.useDelimiter("\\A").next());
      JsonObject obj = tree.getAsJsonObject();
      YSQLStat ysqlStat = new Metrics(obj, true).getYSQLStat(statName);
      if (ysqlStat != null) {
        value.count += ysqlStat.calls;
        value.value += ysqlStat.total_time;
        value.rows += ysqlStat.rows;
      }
      scanner.close();
    }
    return value;
  }

  protected void verifyStatementStat(Statement stmt, String sql, String statName,
                                     int stmtMetricDelta, boolean validStmt) throws Exception {
    long oldValue = 0;
    if (statName != null) {
      oldValue = getStatementStat(statName).count;
    }

    if (validStmt) {
      stmt.execute(sql);
    } else {
      runInvalidQuery(stmt, sql, "ERROR");
    }

    long newValue = 0;
    if (statName != null) {
      newValue = getStatementStat(statName).count;
    }

    assertEquals(oldValue + stmtMetricDelta, newValue);
  }

  protected void verifyStatementStats(Statement stmt, String sql, String statName,
                                      long numLoops, long oldValue) throws Exception {
    for (int i = 0; i < numLoops; i++) {
      stmt.execute(sql);
    }

    long newValue = getStatementStat(statName).count;
    assertEquals(oldValue + numLoops, newValue);
  }

  protected void verifyStatementStatWithReset(Statement stmt, String sql, String statName,
                                              long numLoopsBeforeReset, long numLoopsAfterReset)
                                                throws Exception {
    long oldValue = getStatementStat(statName).count;
    verifyStatementStats(stmt, sql, statName, numLoopsBeforeReset, oldValue);

    resetStatementStat();

    oldValue = 0;
    verifyStatementStats(stmt, sql, statName, numLoopsAfterReset, oldValue);
  }

  private URL[] getMetricSources(
      Collection<MiniYBDaemon> servers, Function<MiniYBDaemon, Integer> portFetcher)
        throws MalformedURLException {
    URL[] result = new URL[servers.size()];
    int index = 0;
    for (MiniYBDaemon s : servers) {
      result[index++] = new URL(String.format(
          "http://%s:%d/metrics", s.getLocalhostIP(), portFetcher.apply(s)));
    }
    return result;
  }

  protected URL[] getTSMetricSources() throws MalformedURLException {
    return getMetricSources(miniCluster.getTabletServers().values(), (s) -> s.getWebPort());
  }

  protected URL[] getYSQLMetricSources() throws MalformedURLException {
    return getMetricSources(miniCluster.getTabletServers().values(), (s) -> s.getPgsqlWebPort());
  }

  protected URL[] getMasterMetricSources() throws MalformedURLException {
    return getMetricSources(miniCluster.getMasters().values(), (s) -> s.getWebPort());
  }

  private JsonArray[] getRawMetric(URL[] sources) throws Exception {
    Collection<MiniYBDaemon> servers = miniCluster.getTabletServers().values();
    JsonArray[] result = new JsonArray[servers.size()];
    int index = 0;
    for (URL url : sources) {
      URLConnection connection = url.openConnection();
      connection.setUseCaches(false);
      Scanner scanner = new Scanner(connection.getInputStream());
      result[index++] =
          JsonParser.parseString(scanner.useDelimiter("\\A").next()).getAsJsonArray();
      scanner.close();
    }
    return result;
  }

  protected JsonArray[] getRawTSMetric() throws Exception {
    return getRawMetric(getTSMetricSources());
  }

  protected JsonArray[] getRawYSQLMetric() throws Exception {
    return getRawMetric(getYSQLMetricSources());
  }

  protected AggregatedValue getMetric(String metricName) throws Exception {
    AggregatedValue value = new AggregatedValue();
    for (JsonArray rawMetric : getRawYSQLMetric()) {
      JsonObject obj = rawMetric.get(0).getAsJsonObject();
      assertEquals(obj.get("type").getAsString(), "server");
      assertEquals(obj.get("id").getAsString(), "yb.ysqlserver");
      Metrics.YSQLMetric metric = new Metrics(obj).getYSQLMetric(metricName);
      value.count += metric.count;
      value.value += metric.sum;
      value.rows += metric.rows;
    }
    return value;
  }

  protected long getMetricCountForTable(
      URL[] sources, String metricName, String tableName) throws Exception {
    long count = 0;
    for (JsonArray rawMetric : getRawMetric(sources)) {
      for (JsonElement elem : rawMetric.getAsJsonArray()) {
        JsonObject obj = elem.getAsJsonObject();
        if (obj.get("type").getAsString().equals("tablet") &&
            obj.getAsJsonObject("attributes").get("table_name").getAsString().equals(tableName)) {
          for (JsonElement subelem : obj.getAsJsonArray("metrics")) {
            if (!subelem.isJsonObject()) {
              continue;
            }
            JsonObject metric = subelem.getAsJsonObject();
            if (metric.has("name") && metric.get("name").getAsString().equals(metricName)) {
              count += metric.get("value").getAsLong();
            }
          }
        }
      }
    }
    return count;
  }

  protected long getTserverMetricCountForTable(
      String metricName, String tableName) throws Exception {
    return getMetricCountForTable(getTSMetricSources(), metricName, tableName);
  }

  protected AggregatedValue getServerMetric(URL[] sources, String metricName) throws Exception {
    AggregatedValue value = new AggregatedValue();
    for (JsonArray rawMetric : getRawMetric(sources)) {
      for (JsonElement elem : rawMetric.getAsJsonArray()) {
        JsonObject obj = elem.getAsJsonObject();
        if (obj.get("type").getAsString().equals("server")) {
          Metrics.Histogram histogram = new Metrics(obj).getHistogram(metricName);
          value.count += histogram.totalCount;
          value.value += histogram.totalSum;
        }
      }
    }
    return value;
  }

  protected AggregatedValue getReadRPCMetric(URL[] sources) throws Exception {
    return getServerMetric(sources, "handler_latency_yb_tserver_TabletServerService_Read");
  }

  protected AggregatedValue getTServerMetric(String metricName) throws Exception {
    return getServerMetric(getTSMetricSources(), metricName);
  }

  protected List<String> getTabletsForTable(
    String database, String tableName) throws Exception {
    try {
      return YBBackupUtil.getTabletsForTable("ysql." + database, tableName);
    } catch (YBBackupException e) {
      return new ArrayList<>();
    }
  }

  protected String getOwnerForTable(Statement stmt, String tableName) throws Exception {
    return getSingleRow(stmt, "SELECT pg_get_userbyid(relowner) FROM pg_class WHERE relname = '" +
        tableName + "'").getString(0);
  }

  protected String getTablespaceForTable(Statement stmt, String tableName) throws Exception {
    ResultSet rs = stmt.executeQuery(
        "SELECT ts.spcname FROM pg_tablespace ts INNER JOIN pg_class c " +
        "ON ts.oid = c.reltablespace WHERE c.oid = '" + tableName + "'::regclass");
    if (!rs.next()) {
      return null; // No tablespace for the table.
    }

    Row row = Row.fromResultSet(rs);
    assertFalse("Result set has more than one row", rs.next());
    return row.getString(0);
  }

  protected int getNumTableColumns(Statement stmt, String tableName) throws Exception {
    return getSingleRow(stmt, "SELECT relnatts FROM pg_class WHERE relname = '" +
                                     tableName + "'").getInt(0);
  }

  protected long getMetricCounter(String metricName) throws Exception {
    return getMetric(metricName).count;
  }

  private interface MetricFetcher {
    AggregatedValue fetch(String name) throws Exception;
  }

  private static abstract class QueryExecutionMetricChecker {
    private MetricFetcher fetcher;
    private String metricName;
    private AggregatedValue oldValue;

    public QueryExecutionMetricChecker(String metricName, MetricFetcher fetcher) {
      this.fetcher = fetcher;
      this.metricName = metricName;
    }

    public void beforeQueryExecution() throws Exception {
      oldValue = fetcher.fetch(metricName);
    }

    public void afterQueryExecution(String query) throws Exception {
      check(query, metricName, oldValue, fetcher.fetch(metricName));
    }

    protected abstract void check(
      String query, String metricName, AggregatedValue oldValue, AggregatedValue newValue);
  }

  private class MetricCountChecker extends QueryExecutionMetricChecker {
    private long countDelta;

    public MetricCountChecker(String name, MetricFetcher fetcher, long countDelta) {
      super(name, fetcher);
      this.countDelta = countDelta;
    }

    @Override
    public void check(
      String query, String metric, AggregatedValue oldValue, AggregatedValue newValue) {
      assertEquals(
        String.format("'%s' count delta assertion failed for query '%s'", metric, query),
        countDelta, newValue.count - oldValue.count);
    }
  }

  private class MetricRowsChecker extends MetricCountChecker {
    private long rowsDelta;

    public MetricRowsChecker(String name, MetricFetcher fetcher, long countDelta, long rowsDelta) {
      super(name, fetcher, countDelta);
      this.rowsDelta = rowsDelta;
    }

    @Override
    public void check(
      String query, String metric, AggregatedValue oldValue, AggregatedValue newValue) {
      super.check(query, metric, oldValue, newValue);
      assertEquals(
        String.format("'%s' row count delta assertion failed for query '%s'", metric, query),
        rowsDelta, newValue.rows - oldValue.rows);
    }
  }

  /** Time execution of a query. */
  private long verifyQuery(Statement statement,
                           String query,
                           boolean validStmt,
                           QueryExecutionMetricChecker... checkers) throws Exception {
    for (QueryExecutionMetricChecker checker : checkers) {
      checker.beforeQueryExecution();
    }
    final long startTimeMillis = System.currentTimeMillis();
    if (validStmt) {
      statement.execute(query);
    } else {
      runInvalidQuery(statement, query, "ERROR");
    }
    // Check the elapsed time.
    final long result = System.currentTimeMillis() - startTimeMillis;
    for (QueryExecutionMetricChecker checker : checkers) {
      checker.afterQueryExecution(query);
    }
    return result;
  }

  /** Time execution of a query. */
  protected long verifyStatementMetric(
    Statement statement, String query, String metricName, int queryMetricDelta,
    int singleShardTxnMetricDelta, int txnMetricDelta, boolean validStmt) throws Exception {
    return verifyQuery(
        statement, query, validStmt,
        new MetricCountChecker(
            SINGLE_SHARD_TRANSACTIONS_METRIC_DEPRECATED, this::getMetric,
            singleShardTxnMetricDelta),
        new MetricCountChecker(
            SINGLE_SHARD_TRANSACTIONS_METRIC, this::getMetric, singleShardTxnMetricDelta),
        new MetricCountChecker(TRANSACTIONS_METRIC, this::getMetric, txnMetricDelta),
        new MetricCountChecker(metricName, this::getMetric, queryMetricDelta));
  }

  protected void verifyStatementTxnMetric(
    Statement statement, String query, int singleShardTxnMetricDelta) throws Exception {
    verifyQuery(
      statement, query,true,
        new MetricCountChecker(
            SINGLE_SHARD_TRANSACTIONS_METRIC_DEPRECATED, this::getMetric,
            singleShardTxnMetricDelta),
        new MetricCountChecker(
          SINGLE_SHARD_TRANSACTIONS_METRIC, this::getMetric, singleShardTxnMetricDelta));
  }

  protected void verifyStatementMetricRows(
    Statement statement, String query, String metricName,
    int countDelta, int rowsDelta) throws Exception {
    verifyQuery(statement, query, true,
      new MetricRowsChecker(metricName, this::getMetric, countDelta, rowsDelta));
  }

  protected void executeWithTimeout(Statement statement, String sql)
      throws SQLException, TimeoutException, InterruptedException {
    // Maintain our map saying how many statements are being run by each backend pid.
    // Later we can determine (possibly) stuck backends based on this.
    final int backendPid = getPgBackendPid(statement.getConnection());

    AtomicReference<SQLException> sqlExceptionWrapper = new AtomicReference<>();
    boolean timedOut = false;
    try {
      String taskDescription = "SQL statement (PG backend pid: " + backendPid + "): " + sql;
      runWithTimeout(DEFAULT_STATEMENT_TIMEOUT_MS, taskDescription, () -> {
        try {
          statement.execute(sql);
        } catch (SQLException e) {
          sqlExceptionWrapper.set(e);
        }
      });
    } catch (TimeoutException ex) {
      // Record that this backend is possibly "stuck" so we can force a core dump and examine it.
      stuckBackendPidsConcMap.add(backendPid);
      timedOut = true;
      throw ex;
    } finally {
      // Make sure we propagate the SQLException. But TimeoutException takes precedence.
      if (!timedOut && sqlExceptionWrapper.get() != null) {
        throw sqlExceptionWrapper.get();
      }
    }
  }

  public class PgTxnState {
    public PgTxnState(Connection connection, String connectionName) {
      this.connection = connection;
      this.connectionName = connectionName;
    }

    boolean isFinished() {
      return stmtExecuted != null &&
          beforeCommitState != null &&
          afterCommitState != null &&
          committed != null;
    }

    public boolean isSuccess() {
      return isFinished() &&
          stmtExecuted &&
          TransactionState.OPEN == beforeCommitState &&
          committed &&
          TransactionState.IDLE == afterCommitState;
    }

    public boolean isFailure() {
      if (!isFinished()) {
        return false;
      }

      // We have two cases:
      // 1. If stmt execution succeeded but commit failed.
      // 2. If stmt exec failed. Then txn should be in failed state and commit should succeed (but
      //    effectively do a rollback/abort).
      if (stmtExecuted) {
        return TransactionState.OPEN == beforeCommitState &&
            !committed &&
            TransactionState.IDLE == afterCommitState;

      } else {
        return TransactionState.FAILED == beforeCommitState &&
            committed &&
            TransactionState.IDLE == afterCommitState;
      }
    }

    @Override
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append("PgTxnState: ").append(connectionName).append("\n");
      sb.append("{\n");
      sb.append("  stmtExecuted: ").append(String.valueOf(stmtExecuted)).append("\n");
      sb.append("  beforeCommitState: ").append(String.valueOf(beforeCommitState)).append("\n");
      sb.append("  committed: ").append(String.valueOf(committed)).append("\n");
      sb.append("  afterCommitState: ").append(String.valueOf(afterCommitState)).append("\n");
      sb.append("}\n");
      return sb.toString();
    }

    private Statement getStatement() throws SQLException {
      if (statement != null) {
        return statement;
      }
      return connection.createStatement();
    }

    private String connectionName;
    private Connection connection;
    private Statement statement = null;

    private Boolean stmtExecuted = null;
    private TransactionState beforeCommitState = null;
    private Boolean committed = null;
    private TransactionState afterCommitState = null;
  }


  protected void executeWithTxnState(PgTxnState txnState, String sql) throws Exception {
    boolean previousStmtFailed = Boolean.FALSE.equals(txnState.stmtExecuted);
    txnState.stmtExecuted = false;
    try {
      executeWithTimeout(txnState.getStatement(), sql);
      txnState.stmtExecuted = !previousStmtFailed;
    } catch (PSQLException ex) {
      // TODO: validate the exception message.
      // Not reporting a stack trace here on purpose, because this will happen a lot in a test.
      LOG.info("Error while inserting on the second connection:" + ex.getMessage());
    }
  }

  protected void commitWithTxnState(PgTxnState txnState) {
    txnState.beforeCommitState = getPgTxnState(txnState.connection);
    txnState.committed = commitAndCatchException(txnState.connection, txnState.connectionName);
    txnState.afterCommitState = getPgTxnState(txnState.connection);
  }

  //------------------------------------------------------------------------------------------------
  // Test Utilities

  protected static class Row implements Comparable<Row>, Cloneable {
    static Row fromResultSet(ResultSet rs) throws SQLException {
      List<Object> elems = new ArrayList<>();
      List<String> columnNames = new ArrayList<>();
      for (int i = 1; i <= rs.getMetaData().getColumnCount(); i++) {
        elems.add(rs.getObject(i));
        columnNames.add(rs.getMetaData().getColumnLabel(i));
      }
      // Pre-initialize stuff while connection is still available
      for (Object el : elems) {
        // TODO(alex): Store as List to begin with?
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

    Row(Object... elems) {
      Collections.addAll(this.elems, elems);
    }

    Row(List<Object> elems, List<String> columnNames) {
      checkArgument(elems.size() == columnNames.size());
      this.elems = elems;
      this.columnNames = columnNames;
    }

    /** Returns a column name if available, or {@code null} otherwise. */
    String getColumnName(int index) {
      return columnNames.size() > 0 ? columnNames.get(index) : null;
    }

    Object get(int index) {
      return elems.get(index);
    }

    Boolean getBoolean(int index) {
      return (Boolean) elems.get(index);
    }

    Integer getInt(int index) {
      return (Integer) elems.get(index);
    }

    Long getLong(int index) {
      return (Long) elems.get(index);
    }

    Double getDouble(int index) {
      return (Double) elems.get(index);
    }

    String getString(int index) {
      return (String) elems.get(index);
    }

    Float getFloat(int index) {
      return (Float) elems.get(index);
    }

    public boolean elementEquals(int idx, Object value) {
      return compare(elems.get(idx), value) == 0;
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

    //
    // Helpers
    //

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
              + o2 + " (of class " + o2.getClass().getCanonicalName() + ")");
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

  protected List<Row> deepCopyRows(List<Row> rows) {
    List<Row> copy = new ArrayList<>();
    for (Row row : rows) {
      copy.add(row.clone());
    }
    return copy;
  }

  protected Set<Row> getRowSet(ResultSet rs) throws SQLException {
    Set<Row> rows = new HashSet<>();
    while (rs.next()) {
      rows.add(Row.fromResultSet(rs));
    }
    return rows;
  }

  protected static Row getSingleRow(ResultSet rs) throws SQLException {
    assertTrue("Result set has no rows", rs.next());
    Row row = Row.fromResultSet(rs);
    assertFalse("Result set has more than one row", rs.next());
    return row;
  }

  protected List<Row> getRowList(Statement stmt, String query) throws SQLException {
    try (ResultSet rs = stmt.executeQuery(query)) {
      return getRowList(rs);
    }
  }

  protected static int executeSystemTableDml(
      Statement stmt, String dml) throws SQLException {
    return systemTableQueryHelper(stmt, () -> stmt.executeUpdate(dml));
  }

  protected List<Row> getSystemTableRowsList(
      Statement stmt, String query) throws SQLException {
    return systemTableQueryHelper(stmt, () -> {
      try (ResultSet result = stmt.executeQuery(query)) {
        return getRowList(result);
      }
    });
  }

  private static <T> T systemTableQueryHelper(
      Statement stmt, ThrowingCallable<T, SQLException> callable) throws SQLException {
    String allow_non_ddl_pattern = "SET yb_non_ddl_txn_for_sys_tables_allowed=%d";
    stmt.execute(String.format(allow_non_ddl_pattern, 1));
    try {
      return callable.call();
    } finally {
      stmt.execute(String.format(allow_non_ddl_pattern, 0));
    }
  }

  protected static Row getSingleRow(Statement stmt, String query) throws SQLException {
    try (ResultSet rs = stmt.executeQuery(query)) {
      return getSingleRow(rs);
    }
  }

  protected List<Row> getRowList(ResultSet rs) throws SQLException {
    List<Row> rows = new ArrayList<>();
    while (rs.next()) {
      rows.add(Row.fromResultSet(rs));
    }
    return rows;
  }

  protected List<Row> getSortedRowList(ResultSet rs) throws SQLException {
    // Sort all rows and return.
    List<Row> rows = getRowList(rs);
    Collections.sort(rows);
    return rows;
  }

  /**
   * Checks that collections are of the same sizes, printing unexpected and missing rows otherwise.
   */
  protected <T> void assertCollectionSizes(
      String errorPrefix,
      Collection<T> expected,
      Collection<T> actual) {
    if (expected.size() != actual.size()) {
      List<T> unexpected = new ArrayList<>(actual);
      unexpected.removeAll(expected);
      List<T> missing = new ArrayList<>(expected);
      missing.removeAll(actual);
      fail(errorPrefix + " Collection length mismatch: expected<" + expected.size()
          + "> but was:<" + actual.size() + ">"
          + "\nUnexpected rows: " + unexpected
          + "\nMissing rows:    " + missing);
    }
  }

  /** Better alternative to assertEquals that provides more mismatch details. */
  protected void assertRows(List<Row> expected, List<Row> actual) {
    assertRows(null, expected, actual);
  }

  /** Better alternative to assertEquals that provides more mismatch details. */
  protected void assertRows(String messagePrefix, List<Row> expected, List<Row> actual) {
    String fullPrefix = StringUtils.isEmpty(messagePrefix) ? "" : (messagePrefix + ": ");
    assertCollectionSizes(fullPrefix, expected, actual);
    for (int i = 0; i < expected.size(); ++i) {
      assertRow(fullPrefix + "Mismatch at row " + (i + 1) + ": ", expected.get(i), actual.get(i));
    }
  }

  /** Better alternative to assertEquals, which provides more mismatch details. */
  protected void assertRow(String messagePrefix, Row expected, Row actual) {
    assertEquals(messagePrefix
        + "Expected row width mismatch: expected:<" + expected.elems.size()
        + "> but was:<" + actual.elems.size() + ">"
        + "\nExpected row: " + expected
        + "\nActual row:   " + actual,
        expected.elems.size(), actual.elems.size());
    for (int i = 0; i < expected.elems.size(); ++i) {
      String columnNameOrNull = expected.getColumnName(i);
      assertTrue(messagePrefix
          + "Column #" + (i + 1)
          + (columnNameOrNull != null ? " (" + columnNameOrNull + ") " : " ")
          + "mismatch: expected:<" + expected.elems.get(i)
          + "> but was:<" + actual.elems.get(i) + ">"
          + "\nExpected row: " + expected.toString(true /* printColumnNames */)
          + "\nActual row:   " + actual.toString(true /* printColumnNames */),
          expected.elementEquals(i, actual.elems.get(i)));
    }
  }

  protected void assertRow(Row expected, Row actual) {
    assertRow("", expected, actual);
  }

  protected void assertQuery(Statement stmt, String query, Row... expectedRows)
      throws SQLException {
    List<Row> actualRows = getRowList(stmt.executeQuery(query));
    assertEquals(
        "Expected " + expectedRows.length + " rows, got " + actualRows.size() + ": " + actualRows,
        expectedRows.length, actualRows.size());
    assertRows(Arrays.asList(expectedRows), actualRows);
  }

  protected void assertQuery(PreparedStatement stmt, Row... expectedRows)
      throws SQLException {
    List<Row> actualRows = getRowList(stmt.executeQuery());
    assertEquals(
        "Expected " + expectedRows.length + " rows, got " + actualRows.size() + ": " + actualRows,
        expectedRows.length, actualRows.size());
    assertRows(Arrays.asList(expectedRows), actualRows);
  }

  protected void assertNoRows(Statement stmt, String query) throws SQLException {
    List<Row> actualRows = getRowList(stmt.executeQuery(query));
    assertTrue("Expected no results, got " + actualRows, actualRows.isEmpty());
  }

  protected void assertNextRow(ResultSet rs, Object... values) throws SQLException {
    assertTrue(rs.next());
    Row expected = new Row(values);
    Row actual = Row.fromResultSet(rs);
    assertRow(expected, actual);
  }

  protected void assertOneRow(Statement statement,
                              String query,
                              Object... values) throws SQLException {
    try (ResultSet rs = statement.executeQuery(query)) {
      assertNextRow(rs, values);
      assertFalse(rs.next());
    }
  }

  protected void assertRowSet(Statement statement,
                              String query,
                              Set<Row> expectedRows) throws SQLException {
    try (ResultSet rs = statement.executeQuery(query)) {
      assertEquals(expectedRows, getRowSet(rs));
    }
  }

  @SafeVarargs
  protected final <T> Set<T> asSet(T... ts) {
    return Stream.of(ts).collect(Collectors.toSet());
  }

  protected void assertRowList(Statement statement,
                               String query,
                               List<Row> expectedRows) throws SQLException {
    try (ResultSet rs = statement.executeQuery(query)) {
      assertRows(expectedRows, getRowList(rs));
    }
  }

  private boolean doesQueryPlanContainsSubstring(Statement stmt, String query, String substring)
      throws SQLException {

    return getQueryPlanString(stmt, query).contains(substring);
  }

  protected String getQueryPlanString(Statement stmt, String query) throws SQLException {
    LOG.info("EXPLAIN " + query);
    StringBuilder builder = new StringBuilder();
    try (ResultSet rs = stmt.executeQuery("EXPLAIN " + query)) {
      assert (rs.getMetaData().getColumnCount() == 1); // Expecting one string column.
      while (rs.next()) {
        builder.append(rs.getString(1) + "\n");
      }
    }
    LOG.info(builder.toString());
    return builder.toString();
  }

  /** Whether or not this select query uses Index Scan with a given index. */
  protected boolean isIndexScan(Statement stmt, String query, String index)
      throws SQLException {
    return doesQueryPlanContainsSubstring(stmt, query, "Index Scan using " + index);
  }

  /** Whether or not this select query uses Index Only Scan with a given index. */
  protected boolean isIndexOnlyScan(Statement stmt, String query, String index)
      throws SQLException {
    return doesQueryPlanContainsSubstring(stmt, query, "Index Only Scan using " + index);
  }

  /** Whether or not this select query uses Seq Scan. */
  protected boolean isSeqScan(Statement stmt, String query)
      throws SQLException {
    return doesQueryPlanContainsSubstring(stmt, query, "Seq Scan on");
  }

  /**
   * Whether or not this select query requires filtering by Postgres (i.e. not all
   * conditions can be pushed down to YugaByte).
   */
  protected boolean doesNeedPgFiltering(Statement stmt, String query) throws SQLException {
    return doesQueryPlanContainsSubstring(stmt, query, "Filter:");
  }

  /** Whether or not this query pushes down a filter condition */
  protected boolean doesPushdownCondition(Statement stmt, String query) throws SQLException {
    return doesQueryPlanContainsSubstring(stmt, query, "Remote Filter:");
  }

  /**
   * Return whether this select query uses a partitioned index in an IndexScan for ordering.
   */
  protected void isPartitionedOrderedIndexScan(Statement stmt,
                                               String query,
                                               String index)
    throws SQLException {

    String query_plan = getQueryPlanString(stmt, query);
    assertTrue(query_plan.contains("Merge Append"));
    assertTrue(query_plan.contains("Index Scan using " + index));
  }

  /**
   * Return whether this select query uses a partitioned index in an
   * Index Only Scan for ordering.
   */
  protected void isPartitionedOrderedIndexOnlyScan(Statement stmt,
                                                   String query,
                                                   String index)
    throws SQLException {

    String query_plan = getQueryPlanString(stmt, query);
    assertTrue(query_plan.contains("Merge Append"));
    assertTrue(query_plan.contains("Index Only Scan using " + index));
  }

  /**
   * Return whether this select query uses the given index in an
   * Index Scan for ordering.
   */
  protected void isOrderedIndexScan(Statement stmt,
                                    String query,
                                    String index)
    throws SQLException {

    String query_plan = getQueryPlanString(stmt, query);
    assertFalse(query_plan.contains("Sort"));
    assertTrue(query_plan.contains("Index Scan using " + index));
  }

  /**
   * Return whether this select query uses the given index in an
   * Index Only Scan for ordering.
   */
  protected void isOrderedIndexOnlyScan(Statement stmt,
                                        String query,
                                        String index)
    throws SQLException {

    String query_plan = getQueryPlanString(stmt, query);
    assertFalse(query_plan.contains("Sort"));
    assertTrue(query_plan.contains("Index Only Scan using " + index));
  }

  protected void createSimpleTableWithSingleColumnKey(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      String sql =
          "CREATE TABLE " + tableName + "(h bigint PRIMARY KEY, r float, vi int, vs text)";
      LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
      statement.execute(sql);
      LOG.info("Table creation finished: " + tableName);
    }
  }

  protected void createSimpleTable(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      createSimpleTable(statement, tableName);
    }
  }

  protected void createSimpleTable(Statement statement, String tableName) throws SQLException {
    String sql =
        "CREATE TABLE " + tableName + "(h bigint, r float, vi int, vs text, PRIMARY KEY (h, r))";
    LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
    statement.execute(sql);
    LOG.info("Table creation finished: " + tableName);
  }

  protected void createPartitionedTable(Statement stmt,
                                        String tablename,
                                        YSQLPartitionType mode) throws SQLException {
    createPartitionedTable(stmt, tablename, mode, "", "", "");
  }

  protected void createPartitionedTable(Statement stmt,
                                        String tablename,
                                        YSQLPartitionType mode,
                                        String primary_keys) throws SQLException {
    createPartitionedTable(stmt, tablename, mode, primary_keys, "", "");
  }

  protected void createPartitionedTable(Statement stmt,
                                        String tablename,
                                        YSQLPartitionType mode,
                                        String unique_keys,
                                        String unique_includes) throws SQLException {
    createPartitionedTable(stmt, tablename, mode, "", unique_keys, unique_includes);
  }

  protected void createPartitionedTable(Statement stmt,
                                        String tablename,
                                        YSQLPartitionType mode,
                                        String primary_keys,
                                        String unique_keys,
                                        String unique_includes)
    throws SQLException {

    String pk_constraint = primary_keys.isEmpty() ? "" : (", PRIMARY KEY(" + primary_keys  + ")");

    String unique_constraint = "";
    if (!unique_keys.isEmpty()) {
      unique_constraint = ", UNIQUE(" + unique_keys + ")";
      if (!unique_includes.isEmpty()) {
        unique_constraint += " INCLUDE (" + unique_includes + ")";
      }
    }
    unique_constraint += ")";

    final String create_sql = "CREATE TABLE " + tablename +
      "(k1 int, k2 text, k3 int, v1 int, v2 text" + pk_constraint +
      unique_constraint + " PARTITION BY " + mode + " (k1)";

    LOG.info("Creating table " + tablename + ", SQL statement: " + create_sql);
    stmt.execute(create_sql);
    LOG.info("Table creation finished: " + tablename);
  }

  protected void createPartition(Statement stmt,
                                 String tablename,
                                 YSQLPartitionType mode,
                                 int partIndex) throws SQLException {

    createPartition(stmt, tablename, mode, partIndex, "", "", "");
  }

  protected void createPartition(Statement stmt,
                                 String tablename,
                                 YSQLPartitionType mode,
                                 int partIndex,
                                 String primary_keys) throws SQLException {
    createPartition(stmt, tablename, mode, partIndex, primary_keys, "", "");
  }

  protected void createPartition(Statement stmt,
                                 String tablename,
                                 YSQLPartitionType mode,
                                 int partIndex,
                                 String unique_keys,
                                 String unique_includes) throws SQLException {
    createPartition(stmt, tablename, mode, partIndex, "", unique_keys, unique_includes);
  }

  protected void createPartition(Statement stmt,
                                 String tablename,
                                 YSQLPartitionType mode,
                                 int partIndex,
                                 String primary_keys,
                                 String unique_keys,
                                 String unique_includes) throws SQLException {
    String partition_clause = "";
    if (mode.equals(YSQLPartitionType.HASH)) {
      partition_clause = "WITH (modulus 2, remainder " + (partIndex - 1) + ")";
    } else if (mode.equals(YSQLPartitionType.LIST)) {
      partition_clause = "IN (" + partIndex + ")";
    } else {
      partition_clause = "FROM (" + partIndex + ") TO (" + (partIndex + 1) + ")";
    }

    String pk_constraint = primary_keys.isEmpty() ? "" : (", PRIMARY KEY(" + primary_keys  + ")");

    String unique_constraint = "";
    if (!unique_keys.isEmpty()) {
      unique_constraint = ", UNIQUE(" + unique_keys + ")";
      if (!unique_includes.isEmpty()) {
        unique_constraint += " INCLUDE (" + unique_includes + ")";
      }
    }
    unique_constraint += ")";

    final String create_sql = "CREATE TABLE " + tablename + "_" + partIndex +
      " PARTITION OF " + tablename + "(k1, k2, k3, v1, v2" + pk_constraint +
      unique_constraint + " FOR VALUES " + partition_clause;

    LOG.info("Creating table " + tablename + ", SQL statement: " + create_sql);
    stmt.execute(create_sql);
    LOG.info("Table creation finished: " + tablename);
  }

  /**
   * @param statement       The statement used to execute the query.
   * @param query           The query string.
   * @param errorSubstrings An array of (case-insensitive) substrings of the expected error
   *                        messages.
   */
  protected void runInvalidQuery(Statement statement, String query, String... errorSubstrings) {
    runInvalidQuery(statement, query, false, errorSubstrings);
  }

  /**
   * @param statement       The statement used to execute the query.
   * @param query           The query string.
   * @param matchAll        A flag to indicate whether all errorSubstrings must be present (true)
   *                       or if only one is sufficient (false).
   * @param errorSubstrings An array of (case-insensitive) substrings of the expected error
   *                        messages.
   */
  protected void runInvalidQuery(Statement statement, String query, boolean matchAll,
                                 String... errorSubstrings) {
    try {
      statement.execute(query);
      fail(String.format("Statement did not fail: %s", query));
    } catch (SQLException e) {
      if (matchAll) {
        List<String> missingSubstrings = new ArrayList<>();

        for (String errorSubstring : errorSubstrings) {
          if (!StringUtils.containsIgnoreCase(e.getMessage(), errorSubstring)) {
            missingSubstrings.add(errorSubstring);
          }
        }

        if (!missingSubstrings.isEmpty()) {
          String missingSubstringsStr = missingSubstrings.stream().map(s -> "'" + s + "'")
            .collect(Collectors.joining(", "));
          fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: %s",
            e.getMessage(), missingSubstringsStr));
        }
      } else {
        for (String errorSubstring : errorSubstrings) {
          if (StringUtils.containsIgnoreCase(e.getMessage(), errorSubstring)) {
            LOG.info("Expected exception", e);
            return;
          }
        }

        final String expectedErrMsg = Arrays.asList(errorSubstrings).stream().map(i -> "'" + i +
            "'").collect(Collectors.joining(", "));
        final String failMessage = String.format("Unexpected Error Message. Got: '%s', Expected " +
          "to contain one of the error messages: %s.", e.getMessage(), expectedErrMsg);
        fail(failMessage);
      }
    }
  }

  protected void runInvalidSystemQuery(Statement stmt, String query, String errorSubstring)
      throws Exception {
    systemTableQueryHelper(stmt, () -> {
      runInvalidQuery(stmt, query, errorSubstring);
      return 0;
    });
  }

  /**
   * Verify that a (write) query succeeds with a warning matching the given substring.
   * @param statement The statement used to execute the query.
   * @param query The query string.
   * @param warningSubstring A (case-insensitive) substring of the expected warning message.
   */
  protected void verifyStatementWarning(Statement statement,
                                        String query,
                                        String warningSubstring) throws SQLException {
    statement.execute(query);
    SQLWarning warning = statement.getWarnings();
    assertNotEquals("Expected (at least) one warning", null, warning);
    assertTrue(String.format("Unexpected Warning Message. Got: '%s', expected to contain : '%s",
                             warning.getMessage(), warningSubstring),
               StringUtils.containsIgnoreCase(warning.getMessage(), warningSubstring));
    assertEquals("Expected (at most) one warning", null, warning.getNextWarning());
  }

  /**
   * Verify that a (write) query succeeds with multiple lines of warnings.
   * @param statement The statement used to execute the query.
   * @param query The query string.
   * @param warningSubstring A (case-insensitive) list of substrings of expected warning messages.
   */
  protected void verifyStatementWarnings(Statement statement,
                                        String query,
                                        List<String> warningSubstrings) throws SQLException {
    int warningLineIndex = 0;
    int expectedWarningCount = warningSubstrings.size();
    statement.execute(query);
    SQLWarning warning = statement.getWarnings();

    // make sure number of warnings match expected number of warnings
    int warningCount = 0;
    while (warning != null) {
      warningCount++;
      warning = warning.getNextWarning();
    }
    assertEquals("Expected " + expectedWarningCount + " warnings.", expectedWarningCount,
      warningCount);

    // check each warning matches expected list of warnings
    warning = statement.getWarnings();
    while (warning != null) {
      assertTrue(String.format("Unexpected Warning Message. Got: '%s', expected to contain : '%s",
                            warning.getMessage(), warningSubstrings.get(warningLineIndex)),
                 StringUtils.containsIgnoreCase(warning.getMessage(),
                            warningSubstrings.get(warningLineIndex)));
      warning = warning.getNextWarning();
      warningLineIndex++;
    }

  }

  protected String getSimpleTableCreationStatement(
      String tableName,
      String valueColumnName,
      PartitioningMode partitioningMode) {
    String firstColumnIndexMode;
    if (partitioningMode == PartitioningMode.HASH) {
      firstColumnIndexMode = "HASH";
    } else {
      firstColumnIndexMode = "ASC";
    }
    return "CREATE TABLE " + tableName + "(h int, r int, " + valueColumnName + " int, " +
        "PRIMARY KEY (h " + firstColumnIndexMode + ", r))";
  }

  protected void createSimpleTable(
      String tableName,
      String valueColumnName,
      PartitioningMode partitioningMode) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      String sql = getSimpleTableCreationStatement(tableName, valueColumnName, partitioningMode);
      LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
      statement.execute(sql);
      LOG.info("Table creation finished: " + tableName);
    }
  }

  protected void createSimpleTable(String tableName, String valueColumnName) throws SQLException {
    createSimpleTable(tableName, valueColumnName, PartitioningMode.HASH);
  }

  protected List<Row> setupSimpleTable(String tableName) throws SQLException {
    List<Row> allRows = new ArrayList<>();
    try (Statement statement = connection.createStatement()) {
      createSimpleTable(statement, tableName);
      String insertTemplate = "INSERT INTO %s(h, r, vi, vs) VALUES (%d, %f, %d, '%s')";

      for (int h = 0; h < 10; h++) {
        for (int r = 0; r < 10; r++) {
          statement.execute(String.format(insertTemplate, tableName,
                                          h, r + 0.5, h * 10 + r, "v" + h + r));
          allRows.add(new Row((long) h,
                              r + 0.5,
                              h * 10 + r,
                              "v" + h + r));
        }
      }
    }

    // Sort inserted rows and return.
    Collections.sort(allRows);
    return allRows;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    // initdb takes a really long time on macOS in debug mode.
    return 1200;
  }

  void waitForTServerHeartbeat() throws InterruptedException {
    // Wait an extra heartbeat interval to avoid race conditions due to deviations
    // in the real heartbeat frequency (due to latency, scheduling, etc.).
    Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS * 2);
  }

  /** Run a query and check row-count. */
  public int runQueryWithRowCount(Statement stmt, String query, int expectedRowCount)
      throws Exception {
    // Query and check row count.
    int rowCount = 0;
    try (ResultSet rs = stmt.executeQuery(query)) {
      while (rs.next()) {
        rowCount++;
      }
    }
    if (expectedRowCount >= 0) {
      // Caller wants to assert row-count.
      assertEquals(expectedRowCount, rowCount);
    } else {
      LOG.info(String.format("Exec query: row count = %d", rowCount));
    }

    return rowCount;
  }

  /** Run a query and check row-count. */
  private void runQueryWithRowCount(PreparedStatement pstmt, int expectedRowCount)
      throws Exception {
    // Query and check row count.
    int rowCount = 0;
    try (ResultSet rs = pstmt.executeQuery()) {
      while (rs.next()) {
        rowCount++;
      }
    }
    if (expectedRowCount >= 0) {
      // Caller wants to assert row-count.
      assertEquals(expectedRowCount, rowCount);
    } else {
      LOG.info(String.format("Exec query: row count = %d", rowCount));
    }
  }

  /** Time execution of a query. */
  protected long timeQueryWithRowCount(Statement stmt,
                                       String query,
                                       int expectedRowCount,
                                       int numberOfRuns) throws Exception {
    LOG.info(String.format("Exec statement: %s", stmt));

    // Not timing the first query run as its result is not predictable.
    runQueryWithRowCount(stmt, query, expectedRowCount);

    // Seek average run-time for a few different run.
    final long startTimeMillis = System.currentTimeMillis();
    for (int qrun = 0; qrun < numberOfRuns; qrun++) {
      runQueryWithRowCount(stmt, query, expectedRowCount);
    }

    // Check the elapsed time.
    long result = System.currentTimeMillis() - startTimeMillis;
    LOG.info(String.format("Ran query %d times. Total elapsed time = %d msecs",
        numberOfRuns, result));
    return result;
  }

  /** Time execution of a query. */
  protected long timeQueryWithRowCount(
      PreparedStatement pstmt,
      int expectedRowCount,
      int numberOfRuns) throws Exception {
    LOG.info("Exec prepared statement");

    // Not timing the first query run as its result is not predictable.
    runQueryWithRowCount(pstmt, expectedRowCount);

    // Seek average run-time for a few different run.
    final long startTimeMillis = System.currentTimeMillis();
    for (int qrun = 0; qrun < numberOfRuns; qrun++) {
      runQueryWithRowCount(pstmt, expectedRowCount);
    }

    // Check the elapsed time.
    long result = System.currentTimeMillis() - startTimeMillis;
    LOG.info(String.format("Ran statement %d times. Total elapsed time = %d msecs",
        numberOfRuns, result));
    return result;
  }

  /** Time execution of a statement. */
  protected long timeStatement(String sql, int numberOfRuns)
      throws Exception {
    LOG.info(String.format("Exec statement: %s", sql));

    // Not timing the first query run as its result is not predictable.
    try (Statement statement = connection.createStatement()) {
      statement.executeUpdate(sql);
    }

    // Seek average run-time for a few different run.
    final long startTimeMillis = System.currentTimeMillis();
    try (Statement statement = connection.createStatement()) {
      for (int qrun = 0; qrun < numberOfRuns; qrun++) {
        statement.executeUpdate(sql);
      }
    }

    // Check the elapsed time.
    long result = System.currentTimeMillis() - startTimeMillis;
    LOG.info(String.format("Ran statement %d times. Total elapsed time = %d msecs",
        numberOfRuns, result));
    return result;
  }

  /** Time execution of a statement. */
  protected long timeStatement(PreparedStatement pstmt, int numberOfRuns)
      throws Exception {
    LOG.info("Exec prepared statement");

    // Not timing the first query run as its result is not predictable.
    pstmt.executeUpdate();

    // Seek average run-time for a few different run.
    final long startTimeMillis = System.currentTimeMillis();
    for (int qrun = 0; qrun < numberOfRuns; qrun++) {
      pstmt.executeUpdate();
    }

    // Check the elapsed time.
    long result = System.currentTimeMillis() - startTimeMillis;
    LOG.info(String.format("Ran statement %d times. Total elapsed time = %d msecs",
        numberOfRuns, result));
    return result;
  }

  /** Time execution of a query. */
  protected long assertQueryRuntimeWithRowCount(
      Statement stmt,
      String query,
      int expectedRowCount,
      int numberOfRuns,
      long maxTotalMillis) throws Exception {
    long elapsedMillis = timeQueryWithRowCount(stmt, query, expectedRowCount, numberOfRuns);
    assertTrue(
        String.format("Query '%s' took %d ms! Expected %d ms at most", stmt, elapsedMillis,
            maxTotalMillis),
        elapsedMillis <= maxTotalMillis);
    return elapsedMillis;
  }

  /** Time execution of a query. */
  protected long assertQueryRuntimeWithRowCount(
      PreparedStatement pstmt,
      int expectedRowCount,
      int numberOfRuns,
      long maxTotalMillis) throws Exception {
    long elapsedMillis = timeQueryWithRowCount(pstmt, expectedRowCount, numberOfRuns);
    assertTrue(
        String.format("Query took %d ms! Expected %d ms at most", elapsedMillis, maxTotalMillis),
        elapsedMillis <= maxTotalMillis);
    return elapsedMillis;
  }

  /** Time execution of a statement. */
  protected long assertStatementRuntime(
      String sql,
      int numberOfRuns,
      long maxTotalMillis) throws Exception {
    long elapsedMillis = timeStatement(sql, numberOfRuns);
    assertTrue(
        String.format("Statement '%s' took %d ms! Expected %d ms at most", sql, elapsedMillis,
            maxTotalMillis),
        elapsedMillis <= maxTotalMillis);
    return elapsedMillis;
  }

  /** Time execution of a statement. */
  protected long assertStatementRuntime(
      PreparedStatement pstmt,
      int numberOfRuns,
      long maxTotalMillis) throws Exception {
    long elapsedMillis = timeStatement(pstmt, numberOfRuns);
    assertTrue(
        String.format("Statement took %d ms! Expected %d ms at most", elapsedMillis,
            maxTotalMillis),
        elapsedMillis <= maxTotalMillis);
    return elapsedMillis;
  }

  /** UUID of the first table with specified name. **/
  private String getTableUUID(String tableName)  throws Exception {
    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table :
        miniCluster.getClient().getTablesList().getTableInfoList()) {
      if (table.getName().equals(tableName)) {
        return table.getId().toStringUtf8();
      }
    }
    throw new Exception(String.format("YSQL table ''%s' not found", tableName));
  }

  protected RocksDBMetrics getRocksDBMetric(String tableName) throws Exception {
    return getRocksDBMetricByTableUUID(getTableUUID(tableName));
  }

  protected int getTableCounterMetric(String tableName,
                                      String metricName) throws Exception {
    return getTableCounterMetricByTableUUID(getTableUUID(tableName), metricName);
  }

  protected String getExplainAnalyzeOutput(Statement stmt, String query) throws Exception {
    try (ResultSet rs = stmt.executeQuery("EXPLAIN ANALYZE " + query)) {
      StringBuilder sb = new StringBuilder();
      while (rs.next()) {
        sb.append(rs.getString(1)).append("\n");
      }
      return sb.toString().trim();
    }
  }

  static final Pattern roundtrips_pattern = Pattern.compile("Storage Read Requests: (\\d+)\\s*$");

  protected Long getNumStorageRoundtrips(Statement stmt, String query) throws Exception {
    try (ResultSet rs = stmt.executeQuery(
        "EXPLAIN (ANALYZE, DIST, COSTS OFF, TIMING OFF) " + query)) {
      while (rs.next()) {
        String line = rs.getString(1);
        Matcher m = roundtrips_pattern.matcher(line);
        if (m.find()) {
          return Long.parseLong(m.group(1));
        }
      }
    }
    return null;
  }

  protected Long getNumDocdbRequests(Statement stmt, String query) throws Exception {
    // Executing query once just in case if master catalog cache is not refreshed
    stmt.execute(query);
    Long rpc_count_before =
      getTServerMetric("handler_latency_yb_tserver_PgClientService_Perform").count;
    stmt.execute(query);
    Long rpc_count_after =
      getTServerMetric("handler_latency_yb_tserver_PgClientService_Perform").count;
    return rpc_count_after - rpc_count_before;
  }

  /** Creates a new tserver and returns its id. **/
  protected int spawnTServer() throws Exception {
    int tserverId = miniCluster.getNumTServers();
    miniCluster.startTServer(getTServerFlags());
    return tserverId;
  }

  /** Creates a new tserver with additional flags and returns its id. **/
  protected int spawnTServerWithFlags(Map<String, String> additionalFlags) throws Exception {
    Map<String, String> tserverFlags = getTServerFlags();
    tserverFlags.putAll(additionalFlags);
    int tserverId = miniCluster.getNumTServers();
    miniCluster.startTServer(tserverFlags);
    return tserverId;
  }

  /**
   * Simple helper for {@link #spawnTServerWithFlags(Map)}.
   * <p>
   * Please use {@code ImmutableMap.of} for more arguments!
   */
  protected int spawnTServerWithFlags(
      String additionalFlagKey, String additionalFlagValue) throws Exception {
    return spawnTServerWithFlags(ImmutableMap.of(additionalFlagKey, additionalFlagValue));
  }

  /** Run a process, returning output lines. */
  protected List<String> runProcess(String... args) throws Exception {
    return runProcess(new ProcessBuilder(args));
  }

  /** Run a process, returning output lines. */
  protected List<String> runProcess(List<String> args) throws Exception {
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

  protected HostAndPort getMasterLeaderAddress() {
    return miniCluster.getClient().getLeaderMasterHostAndPort();
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

  /*
   * A helper method to get the current RSS memory (in kilobytes) for a PID.
   */
  protected static long getRssForPid(int pid) throws Exception {
    Process process = Runtime.getRuntime().exec(String.format("ps -p %d -o rss=", pid));
    try (Scanner scanner = new Scanner(process.getInputStream())) {
      return scanner.nextLong();
    }
  }

  protected static void withRoles(Statement statement, RoleSet roles,
                                  ThrowingRunnable runnable) throws Exception {
    for (String role : roles.roleSet) {
      withRole(statement, role, runnable);
    }
  }

  protected static void withRole(Statement statement, String role,
                                 ThrowingRunnable runnable) throws Exception {
    String sessionUser = getSessionUser(statement);
    try {
      statement.execute(String.format("SET SESSION AUTHORIZATION %s", role));
      runnable.run();
    } finally {
      statement.execute(String.format("SET SESSION AUTHORIZATION %s", sessionUser));
    }
  }

  protected static class RoleSet {
    private HashSet<String> roleSet;

    RoleSet(String... roles) {
      this.roleSet = new HashSet<String>();
      this.roleSet.addAll(Lists.newArrayList(roles));
    }

    RoleSet excluding(String... roles) {
      RoleSet newSet = new RoleSet(roles);
      newSet.roleSet.removeAll(Lists.newArrayList(roles));
      return newSet;
    }
  }

  protected static String getSessionUser(Statement statement) throws Exception {
    ResultSet resultSet = statement.executeQuery("SELECT SESSION_USER");
    resultSet.next();
    return resultSet.getString(1);
  }

  protected static String getCurrentUser(Statement statement) throws Exception {
    ResultSet resultSet = statement.executeQuery("SELECT CURRENT_USER");
    resultSet.next();
    return resultSet.getString(1);
  }
}
