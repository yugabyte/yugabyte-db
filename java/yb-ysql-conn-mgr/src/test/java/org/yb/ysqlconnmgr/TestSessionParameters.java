// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;
import static org.yb.AssertionWrappers.assertThrows;

import java.io.File;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.HashSet;
import java.util.Arrays;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.ProcessUtil;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import com.yugabyte.PGConnection;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestSessionParameters extends BaseYsqlConnMgr {

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // Keep the pool size to 2 in order to test how GUC variables are set on
    // different physical connections. Keeping this limit does not affect the
    // rest of the test, and is only used to determinstically switch between
    // physical connections.
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_max_conns_per_db",
          Integer.toString(isTestRunningInWarmupRandomMode() ? 3 : 2));
        // TODO(#25284): Enable the feature eventually. We need to decide whether the multi route
        // pool feature needs to honor the ysql_conn_mgr_max_conns_per_db GFlag and update the test
        // accordingly.
        put("ysql_conn_mgr_enable_multi_route_pool", "false");
      }
    };

    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  private class SessionParameter {
    public String parameterName;
    public String defaultValue;
    public String expectedValue;
    public String expectedInTransactionValue;
    public Set<ExceptionType> exceptionSet;

    public SessionParameter(String parameterName,
                            String defaultValue,
                            String expectedValue,
                            ExceptionType[] exceptionSet) {
      this.parameterName = parameterName;
      this.defaultValue = defaultValue;
      this.expectedValue = expectedValue;
      this.expectedInTransactionValue = expectedValue;
      this.exceptionSet = new HashSet<ExceptionType>();
      this.exceptionSet.addAll(Arrays.asList(exceptionSet));
    }

    // Some session variables are applicable only to transactions.
    public SessionParameter(String parameterName,
                            String defaultValue,
                            String expectedValue,
                            String expectedInTransactionValue,
                            ExceptionType[] exceptionSet) {
      this.parameterName = parameterName;
      this.defaultValue = defaultValue;
      this.expectedValue = expectedValue;
      this.expectedInTransactionValue = expectedInTransactionValue;
      this.exceptionSet = new HashSet<ExceptionType>();
      this.exceptionSet.addAll(Arrays.asList(exceptionSet));
    }
  }

  private static enum QueryType {
    SET,
    RESET,
    SET_LOCAL,
    SET_LOCAL_DEFAULT,
    RESET_ALL
  }

  private static enum ExceptionType {
    SET_UNIQUE,         // Cannot SET to the same value as it already was
    TRANSACTION_SKIP,   // Should be skipped for active transactions
    TRANSACTION_ONLY,   // Can only be modified within a transaction
    AUTH_PARAM,         // Auth-related parameters
    STICKY_PARAM,       // Parameters that need stickiness
    TIME_UNIT,          // Parameters with TIME_UNIT flag enabled
    APPLICATION_NAME,   // Overriden by JDBC driver
    EXTRA_FLOAT_DIGITS, // Overriden by JDBC driver
    YB_CACHE_MAPPING,   // YB stores (maps) certain parameters differently
    INVALID_STARTUP     // Skip while testing startup parameters
  }

  private final SessionParameter[] SessionParameters = {
    new SessionParameter("application_name", "", "test",
      new ExceptionType[] { ExceptionType.APPLICATION_NAME, ExceptionType.INVALID_STARTUP }),
    new SessionParameter("IntervalStyle", "postgres", "sql_standard",
      new ExceptionType[] {}),
    new SessionParameter("search_path", "\"$user\", public", "test",
      new ExceptionType[] {}),
    // JDBC sends TimeZone in the startup packet, and it can't be overridden using startup options
    new SessionParameter("TimeZone", "UTC", "EST",
      new ExceptionType[] { ExceptionType.INVALID_STARTUP }),
    new SessionParameter("default_transaction_isolation", "read committed", "serializable",
      new ExceptionType[] { ExceptionType.YB_CACHE_MAPPING }),
    new SessionParameter("transaction_isolation", "read committed",
      "read committed", "serializable",
      new ExceptionType[] { ExceptionType.YB_CACHE_MAPPING, ExceptionType.TRANSACTION_SKIP,
        ExceptionType.SET_UNIQUE, ExceptionType.INVALID_STARTUP }),
    new SessionParameter("geqo", "on", "off",
      new ExceptionType[] {}),
    new SessionParameter("statement_timeout", "0", "999",
      new ExceptionType[] { ExceptionType.TIME_UNIT }),
    new SessionParameter("lock_timeout", "0", "999",
      new ExceptionType[] { ExceptionType.TIME_UNIT }),
    new SessionParameter("idle_in_transaction_session_timeout", "0", "999",
      new ExceptionType[] { ExceptionType.TIME_UNIT }),
    // JDBC sets this to be default value of 2 in startup packet
    new SessionParameter("extra_float_digits", "2", "3",
      new ExceptionType[] { ExceptionType.EXTRA_FLOAT_DIGITS, ExceptionType.INVALID_STARTUP }),
    new SessionParameter("default_statistics_target", "100", "200",
      new ExceptionType[] {}),
    new SessionParameter("standard_conforming_strings", "on", "off",
      new ExceptionType[] {}),
    new SessionParameter("check_function_bodies", "on", "off",
      new ExceptionType[] {}),
    new SessionParameter("default_transaction_read_only", "off", "on",
      new ExceptionType[] {}),
    new SessionParameter("default_transaction_deferrable", "off", "on",
      new ExceptionType[] {}),
    new SessionParameter("transaction_read_only", "off", "off", "on",
      new ExceptionType[] { ExceptionType.TRANSACTION_ONLY }),

    // Sticky GUC variables make the primary connection sticky, test at the end.

    new SessionParameter("session_authorization", "yugabyte", "test",
      new ExceptionType[] { ExceptionType.AUTH_PARAM, ExceptionType.INVALID_STARTUP,
        ExceptionType.STICKY_PARAM }),
    new SessionParameter("role", "none", "test2",
      new ExceptionType[] { ExceptionType.AUTH_PARAM, ExceptionType.INVALID_STARTUP,
        ExceptionType.STICKY_PARAM }),
    new SessionParameter("default_tablespace", "", "pg_default",
      new ExceptionType[] { ExceptionType.STICKY_PARAM }),
    new SessionParameter("temp_tablespaces", "", "pg_default",
      new ExceptionType[] { ExceptionType.STICKY_PARAM }),
  };

  // Trim the leading millisecond unit from the fetched value, if not 0.
  private static String trimTimeUnitFromParam(String timeString) {
    if(!timeString.equals("0"))
      timeString = timeString.substring(0, timeString.length() - 2);
    return timeString;
  }

  private static String fetchParameterValue(Statement stmt, SessionParameter sp) {
    try (ResultSet rs = stmt.executeQuery(String.format("SHOW %s", sp.parameterName))) {
      assertTrue(String.format("expected one row while fetching %s",
          sp.parameterName), rs.next());
      String returnString = rs.getObject(1, String.class);

      if(sp.exceptionSet.contains(ExceptionType.TIME_UNIT))
        returnString = trimTimeUnitFromParam(returnString);

      return returnString;
    } catch (Exception e) {
      fail(String.format("error while fetching value of %s: %s",
          sp.parameterName, e.getMessage()));
      return "";
    }
  }

  private static void modifyParameterValue(Statement stmt, SessionParameter sp, QueryType qType) {
    String expectedValue = sp.expectedValue;
    String defaultValue = sp.defaultValue;
    final String expectedInTransactionValue = sp.expectedInTransactionValue;
    final String parameterName = sp.parameterName;

    // SET_UNIQUE variables cannot be SET to the same value as its
    // initial value, it is fine to change its value to any arbritrary, allowed
    // value.
    if (sp.exceptionSet.contains(ExceptionType.SET_UNIQUE)) {
      expectedValue = sp.expectedInTransactionValue;
    }

    if (defaultValue.isEmpty() || defaultValue.contains(" ")) {
      defaultValue = String.format("'%s'", defaultValue);
    }
    try {
      switch (qType) {
        case RESET_ALL:
          stmt.execute("RESET ALL");
          break;
        case RESET:
          stmt.execute(String.format("RESET %s", parameterName));
          break;
        case SET:
          stmt.execute(String.format("SET %s = %s", parameterName, expectedValue));
          break;
        case SET_LOCAL:
          stmt.execute(String.format("SET LOCAL %s = %s", parameterName,
              expectedInTransactionValue));
          break;
        case SET_LOCAL_DEFAULT:
          stmt.execute(String.format("SET LOCAL %s = %s", parameterName, defaultValue));
        default:
          break;
      }
    } catch (Exception e) {
      fail(String.format("unable to change value of %s: %s", parameterName, e.getMessage()));
    }
  }

  private String getExpectedValueOnNewConnection (SessionParameter sp) {
    String expectedValue = sp.defaultValue;
    if (sp.exceptionSet.contains(ExceptionType.APPLICATION_NAME))
      expectedValue = "PostgreSQL JDBC Driver"; // JDBC higher precedence
    else if (sp.exceptionSet.contains(ExceptionType.YB_CACHE_MAPPING))
      expectedValue = "read committed";
    else if (sp.exceptionSet.contains(ExceptionType.EXTRA_FLOAT_DIGITS))
      expectedValue = "3"; // JDBC higher precedence
    return expectedValue;
  }

  // Checker function to verify that the values being set do not "leak"
  // onto other logical connections multiplexing on a physical
  // connection.
  private void checkValueOnNewLogicalConnection(SessionParameter sp) {
    final String expectedValue = getExpectedValueOnNewConnection(sp);
    try (Connection conn = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
        Statement stmt = conn.createStatement()) {
      String actualValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format("expected default value %s does not match actual param value %s",
          expectedValue, actualValue), expectedValue.equals(actualValue));
    } catch (Exception e) {
      fail(String.format("unable to read value of %s: %s", sp.parameterName,
          e.getMessage()));
    }
  }

  // Checker function to verify that values set on one logical connection
  // are correctly set when the same logical connection attaches onto a new
  // physical connection.
  private void checkValueOnNewPhysicalConnection(Statement stmt, SessionParameter sp,
                                                String expectedValue) {
    final String parameterName = sp.parameterName;
    String actualValue;
    ResultSet rs;

    try (Connection newConn1 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection newConn2 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection newConn3 = isTestRunningInWarmupRandomMode() ?
            getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect() :
            null;
        Statement newStmt1 = newConn1.createStatement();
        Statement newStmt2 = newConn2.createStatement();
        Statement newStmt3 = isTestRunningInWarmupRandomMode() ?
            newConn3.createStatement() :
            null;) {

      int backendPID = -1;

      // Create new physical connections, but only for non-sticky GUC variables.

      // Just ensure connection is stuck for sticky GUC variables.
      if (sp.exceptionSet.contains(ExceptionType.STICKY_PARAM)) {
        rs = stmt.executeQuery("SELECT pg_backend_pid()");
        assertTrue("backend pid should be non-null", rs.next());
        backendPID = rs.getInt(1);

        // Backend pid on different logical connection should be different.
        rs = newStmt1.executeQuery("SELECT pg_backend_pid()");
        assertTrue("backend pid should be non-null", rs.next());
        assertFalse(String.format(
            "new logical connection should have attached onto new physical connection for %s",
            parameterName), backendPID == rs.getInt(1));
        return;
      }

      // newStmt1 will latch onto the physical conenction the test uses.
      newStmt1.execute("BEGIN");
      newStmt1.execute("SELECT 1");

      // newStmt2 will create a new physical connection.
      newStmt2.execute("BEGIN");
      newStmt2.execute("SELECT 1");

      if (isTestRunningInWarmupRandomMode()) {
      // newStmt3 will create a new physical connection.
        newStmt3.execute("BEGIN");
        newStmt3.execute("SELECT 1");
      }

      // Detach newStmt1 from its physical connection.
      newStmt1.execute("COMMIT");

      // Find the backend PID of the physical connection the test uses.
      rs = stmt.executeQuery("SELECT pg_backend_pid()");
      assertTrue("backend pid should be non-null", rs.next());
      backendPID = rs.getInt(1);

      // Ensure that the variables' state is preserved correctly on the first
      // physical connection.
      actualValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format("expected %s, got %s on new physical connection for %s",
          expectedValue, actualValue, parameterName), expectedValue.equals(actualValue));

      // Lock the first physical connection, and unlock the second one.
      newStmt1.execute("BEGIN");
      newStmt1.execute("SELECT 1");
      newStmt2.execute("COMMIT");

      // Ensure that we are on a new physical connection.
      rs = stmt.executeQuery("SELECT pg_backend_pid()");
      assertTrue("backend pid should be non-null", rs.next());
      assertFalse(String.format(
          "primary connection should have attached onto new physical connection %s",
          parameterName), backendPID == rs.getInt(1));

      // Ensure that the value is correctly set on a different physical
      // connection.
      actualValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format("expected %s, got %s on new physical connection for %s",
          expectedValue, actualValue, parameterName), expectedValue.equals(actualValue));

      // We have established that it does not matter which physical
      // connection the logical connection latches on to; we do not
      // have to strictly ensure that our primary connection sticks to
      // its original physical connection.
      newStmt1.execute("COMMIT");
      if (isTestRunningInWarmupRandomMode())
      {
        newStmt3.execute("COMMIT");
        newConn3.close();
      }
    } catch (Exception e) {
      fail(String.format("failed to check value of %s: %s", parameterName, e.getMessage()));
    }
  }

  private void checkDefaultValues(Statement stmt, SessionParameter sp) {
    final String expectedValue = sp.defaultValue;
    final String fetchedValue = fetchParameterValue(stmt, sp);
    final String parameterName = sp.parameterName;

    assertTrue(String.format("expected %s, but got %s for default value check of %s",
        expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));
  }

  private void checkSetStatements(Statement stmt, SessionParameter sp) {
    final String expectedValue = sp.expectedValue;
    final String parameterName = sp.parameterName;

    modifyParameterValue(stmt, sp, QueryType.SET);
    String fetchedValue = fetchParameterValue(stmt, sp);
    assertTrue(String.format("expected %s, but got %s for SET value check of %s",
        expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));

    checkValueOnNewLogicalConnection(sp);
    checkValueOnNewPhysicalConnection(stmt, sp, expectedValue);
  }

  private void checkResetStatements(Statement stmt, SessionParameter sp) {
    final String expectedValue = sp.defaultValue;
    final String parameterName = sp.parameterName;

    modifyParameterValue(stmt, sp, QueryType.RESET);
    final String fetchedValue = fetchParameterValue(stmt, sp);
    assertTrue(String.format("expected %s, but got %s for RESET value check of %s",
        expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));
  }

  private void checkAbortedTxns(Statement stmt, SessionParameter sp) {
    String expectedValue;
    String fetchedValue;
    final String parameterName = sp.parameterName;

    // GH #21118: Transactions are created as subtransactions, so we cannot test
    // the behaviour of TRANSACTION_SKIP variables in a transactional context.
    if (sp.exceptionSet.contains(ExceptionType.TRANSACTION_SKIP))
      return;

    try {

      // For TRANSACATION_ONLY variables, we can stick to the "expectedValue"
      // during this test.
      if (sp.exceptionSet.contains(ExceptionType.TRANSACTION_ONLY))
        expectedValue = sp.expectedValue;
      else
        expectedValue = sp.expectedInTransactionValue;

      stmt.execute("BEGIN");
      modifyParameterValue(stmt, sp, QueryType.SET);
      fetchedValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format("expected %s, but got %s for in-txn value check of %s",
          expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));

      stmt.execute("ROLLBACK");
      expectedValue = sp.defaultValue;
      fetchedValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format("expected %s, but got %s for aborted txn value check of %s",
          expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));
    } catch (Exception e) {
      fail(String.format("an error occured while checking aborted txns value for %s: %s",
          parameterName, e.getMessage()));
    }
  }

  private void checkSetLocalStatements(Statement stmt, SessionParameter sp) {
    String expectedValue;
    String fetchedValue;
    final String parameterName = sp.parameterName;

    // GH #21118: Transactions are created as subtransactions, so we cannot test
    // the behaviour of TRANSACTION_SKIP variables in a transactional context.
    if (sp.exceptionSet.contains(ExceptionType.TRANSACTION_SKIP))
      return;

    try {
      stmt.execute("BEGIN");
      expectedValue = sp.expectedInTransactionValue;
      modifyParameterValue(stmt, sp, QueryType.SET_LOCAL);
      fetchedValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format("expected %s, but got %s for SET LOCAL value check of %s",
          expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));
      stmt.execute("COMMIT");

      // Expect the variable to go back to default value, previously tested
      // scenario which affected variable state was a RESET.
      expectedValue = sp.defaultValue;
      fetchedValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format(
          "expected %s, but got %s for SET LOCAL->COMMIT value check of %s",
          expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));

      // Check non-default SET LOCAL statements. Only verify the final result
      // since we already verified the ability to use SET LOCAL statements.
      expectedValue = sp.expectedValue;

      modifyParameterValue(stmt, sp, QueryType.SET);
      stmt.execute("BEGIN");
      modifyParameterValue(stmt, sp, QueryType.SET_LOCAL_DEFAULT);
      stmt.execute("COMMIT");

      fetchedValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format(
        "expected %s, but got %s for SET LOCAL DEFAULT->COMMIT value check of %s",
        expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));

      stmt.execute("BEGIN");
      modifyParameterValue(stmt, sp, QueryType.SET_LOCAL_DEFAULT);
      stmt.execute("ROLLBACK");

      fetchedValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format(
        "expected %s, but got %s for SET LOCAL DEFAULT->ROLLBACK value check of %s",
        expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));
    } catch (Exception e) {
      fail(String.format("an error occured while checking SET LOCAL value for %s: %s",
          parameterName, e.getMessage()));
    }
  }

  private void checkResetAllStatements(Statement stmt, SessionParameter sp) {
    String expectedValue;
    String fetchedValue;
    final String parameterName = sp.parameterName;

    modifyParameterValue(stmt, sp, QueryType.SET);
    modifyParameterValue(stmt, sp, QueryType.RESET_ALL);

    // AUTH_PARAM parameters do not RESET with RESET ALL.
    if (sp.exceptionSet.contains(ExceptionType.AUTH_PARAM))
      expectedValue = sp.expectedValue;
    else
      expectedValue = sp.defaultValue;
    fetchedValue = fetchParameterValue(stmt, sp);
    assertTrue(String.format("expected %s, but got %s for RESET ALL value check of %s",
        expectedValue, fetchedValue, parameterName), expectedValue.equals(fetchedValue));
  }

  @Test
  public void testSessionParameters() throws Exception {
    try (Connection conn = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
        Statement stmt = conn.createStatement()) {
      for (int i = 0; i < SessionParameters.length; i++) {
        SessionParameter sp = SessionParameters[i];

        // Set up a role for AUTH_PARAM parameters.
        if (sp.exceptionSet.contains(ExceptionType.AUTH_PARAM))
          stmt.execute(String.format("CREATE ROLE %s", sp.expectedValue));

        // PG JDBC Driver sets an application name,
        // reset to default value for this test.
        if (sp.exceptionSet.contains(ExceptionType.APPLICATION_NAME))
          modifyParameterValue(stmt, sp, QueryType.RESET);

        checkDefaultValues(stmt, sp);
        checkSetStatements(stmt, sp);
        checkResetStatements(stmt, sp);
        checkAbortedTxns(stmt, sp);
        checkSetLocalStatements(stmt, sp);
        checkResetAllStatements(stmt, sp);

        // Reset from the role created to test AUTH_PARAM parameters.
        if (sp.exceptionSet.contains(ExceptionType.AUTH_PARAM)) {
          modifyParameterValue(stmt, sp, QueryType.RESET);
        }
      }
    } catch (Exception e) {
      fail(String.format("Something went wrong: %s", e));
    }
  }

  @Test
  public void testStartupParameters() throws Exception {
    for (int i = 0; i < SessionParameters.length; i++) {
      SessionParameter sp = SessionParameters[i];
      String parameterName = sp.parameterName;
      String expectedValue = sp.expectedValue;

      String valueWithEscapedSpaces = expectedValue.replace(" ", "\\ ");

      if (sp.exceptionSet.contains(ExceptionType.INVALID_STARTUP))
        continue;

      try (Connection conn = getConnectionBuilder()
          .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
          .withOptions(String.format("-c %s=%s", parameterName, valueWithEscapedSpaces))
          .connect();
          Statement stmt = conn.createStatement()) {
        String fetchedValue = fetchParameterValue(stmt, sp);
        assertTrue(String.format("expected value %s for %s, but fetched %s instead",
            expectedValue, parameterName, fetchedValue),
            expectedValue.equals(fetchedValue));
      }
    }
  }

  @Test
  public void testTimeZoneSetting() throws Exception {
    try (Connection conn = getConnectionBuilder()
                               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                               .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET TimeZone = 'Asia/Kolkata'");
      PGConnection pgConn = (PGConnection) conn;
      Map<String, String> params = new HashMap<>(pgConn.getParameterStatuses());

      // Get the timezone directly
      String backendTimeZone = params.get("TimeZone");
      assertTrue("Backend timezone should be set correctly from SET command",
          backendTimeZone.equals("Asia/Kolkata"));
    }

    try (Connection conn = getConnectionBuilder()
                               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                               .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET timezone = 'Asia/Kolkata'");
      PGConnection pgConn = (PGConnection) conn;
      Map<String, String> params = new HashMap<>(pgConn.getParameterStatuses());

      // Get the timezone directly
      String backendTimeZone = params.get("TimeZone");
      assertTrue("Backend timezone should be set correctly from SET command",
          backendTimeZone.equals("Asia/Kolkata"));
    }
  }

  public void testStartupParameterPrecedence() throws Exception {
    // Test the precedence between guc setting specified through "options" key and one specified
    // directly in the startup packet. pgJDBC driver only allows us to use the former method.
    // See getParametersForStartup() function in pgJDBC driver code.
    //
    // To still test for the precedence, we try to set "client_encoding" through "options". The
    // driver internally adds a (client_encoding, UTF8) pair to the startup packet and we test that
    // that setting has a higher precedence
    //
    // Note: We don't test that setting an option through "options" field works, that is
    // accomplished in testStartupParameters() above

    SessionParameter sp =
        new SessionParameter("client_encoding", "SQL_ASCII", "UTF8", new ExceptionType[] {});
    String parameterName = sp.parameterName;
    String expectedValue = sp.expectedValue;

    try (Connection conn = getConnectionBuilder()
                               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                               .withOptions("-c client_encoding=SQL_ASCII")
                               .connect();
         Statement stmt = conn.createStatement()) {
      String fetchedValue = fetchParameterValue(stmt, sp);
      assertTrue(String.format("expected value %s for %s, but fetched %s instead", expectedValue,
                     parameterName, fetchedValue),
          expectedValue.equals(fetchedValue));
    }
  }

  @Test
  public void testStartupParameterCaseInsensitivity() throws Exception {
    // Test that sending a startup parameter in a different case than what Postgres sends in
    // ParameterStatus works correctly.
    // This mainly tests that whatever state the connection manager stores is case-insensitive:
    // either by lowercasing all GUC names in case of auth passthrough or forwarding the startup
    // packet to auth backend and using the name in returned ParameterStatus packet.
    SessionParameter sp =
        new SessionParameter("default_transaction_read_only", "off", "on", new ExceptionType[] {});
    String parameterName = sp.parameterName;
    String expectedValue = sp.expectedValue;

    try (Connection conn = getConnectionBuilder()
                               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                               .withOptions("-c DEFAULT_TRANSACTION_READ_ONLY=on")
                               .connect();
         Statement stmt = conn.createStatement()) {
      String fetchedValue = fetchParameterValue(stmt, sp);

      assertTrue(String.format("expected value %s for %s, but fetched %s instead", expectedValue,
                     parameterName, fetchedValue),
          expectedValue.equals(fetchedValue));
    }
  }

  @Test
  public void testInvalidStartupParameter() throws Exception {
    // Test that setting an invalid parameter in startup packet fails at the time of database
    // connection. We use ysqlsh here since we don't want to execute ANY query after connecting,
    // whereas JDBC tries to execute some SET statements after connecting. With JDBC, we try to
    // deploy the state to a transactional backend which fails and we're not able to distinguish
    // it from connection failure.

    // Sleep for some time to wait for the cluster to come up. We have to do this since this
    // driver doesn't have any retry mechanism
    Thread.sleep(5000);

    File pgBinDir = new File(TestUtils.getBuildRootDir(), "postgres/bin");
    File ysqlshExec = new File(pgBinDir, "ysqlsh");
    final InetSocketAddress postgresAddress = miniCluster.getYsqlConnMgrContactPoints().get(0);

    List<String> args = Arrays.asList(
      ysqlshExec.toString(),
      "-h", postgresAddress.getHostName(),
      "-p", Integer.toString(postgresAddress.getPort()),
      "-U", "yugabyte",
      "-v", "ON_ERROR_STOP=1",
      "-c", "\\q"
    );


    Map<String, String> invalidEnv = new HashMap<String, String>();
    invalidEnv.put("PGOPTIONS", "-c geqo=abc");
    assertThrows(
        IOException.class, () -> { ProcessUtil.runProcess(args, Integer.MAX_VALUE, invalidEnv); });

    Map<String, String> validEnv = new HashMap<String, String>();
    invalidEnv.put("PGOPTIONS", "-c geqo=off");
    ProcessUtil.runProcess(args, Integer.MAX_VALUE, validEnv);
  }

  @Test
  public void testSettingOverrideVariableInStartupPacket() throws Exception {
    // Startup parameters are set with the source PGC_S_CLIENT while SET statements are set
    // with the source PGC_S_SESSION. There can be settings with PGC_S_OVERRIDE which would
    // not be overridden with startup options but would get overriden with SET statements.
    // We test this fact with the GUC session_authorization.

    final String roleName = "test_user_session_auth";

    // Connect as yugabyte user to yugabyte db
    try (
        Connection conn =
            getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .withUser("yugabyte").withDatabase("yugabyte").connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute(String.format("CREATE ROLE %s WITH login", roleName));
    }

    // Connect as test user to yugabyte db
    try (
        Connection connTestUser =
            getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .withUser(roleName).withDatabase("yugabyte").connect();
        Statement stmtTestUser = connTestUser.createStatement()) {
      try (ResultSet rs = stmtTestUser.executeQuery("SELECT 1")) {
        assertTrue("SELECT 1 should return a row", rs.next());
      }
    }

    // Execute a global DDL as a workaround for role not applying when startup packet is processed
    try (
        Connection conn =
            getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .withUser("yugabyte").withDatabase("yugabyte").connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute(
          "create role tmp1; grant tmp1 to yugabyte; revoke tmp1 from yugabyte; drop role tmp1;");
    }

    // Try to set session authorization in startup packet
    String sessionAuthOption = String.format("-c session_authorization=%s", roleName);

    try (
        Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).withUser("yugabyte")
            .withDatabase("yugabyte").withOptions(sessionAuthOption).connect();
        Statement stmt = conn.createStatement()) {

      try (ResultSet rs = stmt.executeQuery("SHOW SESSION AUTHORIZATION")) {
        assertTrue("SHOW SESSION AUTHORIZATION should return a row", rs.next());
        String actualSessionAuth = rs.getString(1);
        assertEquals("yugabyte", actualSessionAuth);
      }
    }
  }
}
