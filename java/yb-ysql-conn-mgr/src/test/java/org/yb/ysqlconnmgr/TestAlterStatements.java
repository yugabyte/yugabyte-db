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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBCluster;
import org.yb.pgsql.ConnectionEndpoint;

/**
 * Tests for connection manager's handling of ALTER DATABASE SET and ALTER ROLE SET statements with
 * different adoption strategies and TTL values.
 *
 * Tests are organized by (strategy, TTL) combinations, and each test runs all 4 ALTER types in
 * precedence order (lowest to highest) to minimize cluster restarts.
 */
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestAlterStatements extends BaseYsqlConnMgr {

    // Reduced timeout since we now have only 6 tests with 1 restart each (vs 36 restarts before)
    @Override
    public int getTestMethodTimeoutSec() {
        return 300;
    }

    // Test GUC parameter - temp_file_limit is easy to verify via SHOW
    private static final String TEST_GUC = "temp_file_limit";
    private static final String DEFAULT_GUC_VALUE = "1GB"; // Default value

    // GUC values in precedence order - each ALTER type uses a different value
    // This allows testing without RESET since higher precedence ALTERs override lower ones
    private static final String[] GUC_VALUES = {"4MB", "8MB", "16MB", "32MB"};
    private static final String[] GUC_VALUES_RAW = {"4096", "8192", "16384", "32768"};

    // Test databases and roles
    private static final String TEST_DB1 = "test_alter_db1";
    private static final String TEST_DB2 = "test_alter_db2";
    private static final String TEST_ROLE1 = "test_alter_role1";
    private static final String TEST_ROLE2 = "test_alter_role2";

    // Adoption strategies
    private static final String STRATEGY_FLUCTUATING = "fluctuating";
    private static final String STRATEGY_GRADUAL = "gradual";
    private static final String STRATEGY_CONNECTION_STATIC = "connection_static";

    // TTL values to test
    private static final int TTL_NO_LIMIT = -1;
    private static final int TTL_IMMEDIATE = 0;
    private static final int TTL_5_SECONDS = 5000;

    // Wait times
    private static final int WAIT_FOR_TTL_MINUS_1_MS = 10000; // 10 seconds for TTL=-1 verification

    /**
     * Enum for different ALTER statement types, ordered by PostgreSQL precedence (lowest to
     * highest)
     */
    private enum AlterType {
        ROLE_ALL(0), // ALTER ROLE ALL SET <var> = <val> (lowest precedence)
        DATABASE(1), // ALTER DATABASE <db> SET <var> = <val>
        ROLE(2), // ALTER ROLE <role> SET <var> = <val>
        ROLE_IN_DATABASE(3); // ALTER ROLE <role> IN DATABASE <db> SET <var> = <val> (highest)

        final int precedenceIndex;

        AlterType(int precedenceIndex) {
            this.precedenceIndex = precedenceIndex;
        }
    }

    // ALTER types in precedence order (lowest to highest)
    private static final AlterType[] PRECEDENCE_ORDER =
            {AlterType.ROLE_ALL, AlterType.DATABASE, AlterType.ROLE, AlterType.ROLE_IN_DATABASE};

    /**
     * Helper class to hold a connection and its associated statement
     */
    private static class ConnectionContext {
        final Connection connection;
        final Statement statement;
        final String database;
        final String role;

        ConnectionContext(Connection conn, Statement stmt, String db, String role) {
            this.connection = conn;
            this.statement = stmt;
            this.database = db;
            this.role = role;
        }

        void close() {
            try {
                if (statement != null)
                    statement.close();
                if (connection != null)
                    connection.close();
            } catch (SQLException e) {
                // Ignore close errors
            }
        }
    }

    /**
     * Tracks expected GUC values for each route (db/role combination). Updated as ALTER statements
     * are executed in precedence order.
     */
    private static class ExpectedValues {
        private final Map<String, String> routeToValue = new HashMap<>();

        ExpectedValues() {
            // Initialize all routes to default value
            for (String db : new String[] {TEST_DB1, TEST_DB2}) {
                for (String role : new String[] {TEST_ROLE1, TEST_ROLE2}) {
                    routeToValue.put(makeKey(db, role), DEFAULT_GUC_VALUE);
                }
            }
        }

        private static String makeKey(String db, String role) {
            return db + "/" + role;
        }

        /**
         * Update expected values based on the ALTER type that was just executed.
         */
        void updateForAlter(AlterType type, String newValue) {
            switch (type) {
                case ROLE_ALL:
                    // Affects all routes
                    for (String key : routeToValue.keySet()) {
                        routeToValue.put(key, newValue);
                    }
                    break;
                case DATABASE:
                    // Affects TEST_DB1 routes only
                    routeToValue.put(makeKey(TEST_DB1, TEST_ROLE1), newValue);
                    routeToValue.put(makeKey(TEST_DB1, TEST_ROLE2), newValue);
                    break;
                case ROLE:
                    // Affects TEST_ROLE1 routes only
                    routeToValue.put(makeKey(TEST_DB1, TEST_ROLE1), newValue);
                    routeToValue.put(makeKey(TEST_DB2, TEST_ROLE1), newValue);
                    break;
                case ROLE_IN_DATABASE:
                    // Affects only TEST_DB1/TEST_ROLE1
                    routeToValue.put(makeKey(TEST_DB1, TEST_ROLE1), newValue);
                    break;
            }
        }

        String getExpected(String db, String role) {
            return routeToValue.get(makeKey(db, role));
        }

        /**
         * Create a copy of current expected values (for pre-ALTER snapshot)
         */
        ExpectedValues copy() {
            ExpectedValues copy = new ExpectedValues();
            copy.routeToValue.putAll(this.routeToValue);
            return copy;
        }
    }

    /**
     * Restart cluster with specific ALTER GUC adoption strategy and TTL settings
     */
    private void restartClusterWithAlterGucFlags(String strategy, int ttlMs) throws Exception {
        Map<String, String> tserverFlags = new HashMap<>();
        tserverFlags.put("allowed_preview_flags_csv", "ysql_conn_mgr_alter_guc_adoption_strategy,"
                + "ysql_conn_mgr_alter_guc_stale_backend_ttl_ms");
        tserverFlags.put("ysql_conn_mgr_alter_guc_adoption_strategy", strategy);
        tserverFlags.put("ysql_conn_mgr_alter_guc_stale_backend_ttl_ms", Integer.toString(ttlMs));
        restartClusterWithAdditionalFlags(java.util.Collections.emptyMap(), tserverFlags);
    }

    /**
     * Create test databases and roles
     */
    private void createTestDbsAndRoles() throws Exception {
        try (Connection conn = getConnectionBuilder()
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
                Statement stmt = conn.createStatement()) {
            // Create databases
            stmt.execute("CREATE DATABASE " + TEST_DB1);
            stmt.execute("CREATE DATABASE " + TEST_DB2);

            // Create roles with login privilege
            stmt.execute("CREATE ROLE " + TEST_ROLE1 + " LOGIN");
            stmt.execute("CREATE ROLE " + TEST_ROLE2 + " LOGIN");

            // Grant connect privileges
            stmt.execute("GRANT CONNECT ON DATABASE " + TEST_DB1 + " TO " + TEST_ROLE1);
            stmt.execute("GRANT CONNECT ON DATABASE " + TEST_DB1 + " TO " + TEST_ROLE2);
            stmt.execute("GRANT CONNECT ON DATABASE " + TEST_DB2 + " TO " + TEST_ROLE1);
            stmt.execute("GRANT CONNECT ON DATABASE " + TEST_DB2 + " TO " + TEST_ROLE2);
        }

        // Wait for role/db info to propagate across cluster
        waitForDbAndRoleInfoToPropagate();
    }

    /**
     * Clean up test databases and roles
     */
    private void cleanupTestDbsAndRoles() {
        try (Connection conn = getConnectionBuilder()
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
                Statement stmt = conn.createStatement()) {
            // Drop databases (ignore errors if they don't exist)
            tryExecute(stmt, "DROP DATABASE IF EXISTS " + TEST_DB1);
            tryExecute(stmt, "DROP DATABASE IF EXISTS " + TEST_DB2);

            // Drop roles
            tryExecute(stmt, "DROP ROLE IF EXISTS " + TEST_ROLE1);
            tryExecute(stmt, "DROP ROLE IF EXISTS " + TEST_ROLE2);
        } catch (Exception e) {
            LOG.warn("Error during cleanup", e);
        }
    }

    private void tryExecute(Statement stmt, String sql) {
        try {
            stmt.execute(sql);
        } catch (SQLException e) {
            LOG.warn("Error executing: " + sql, e);
        }
    }

    /**
     * Wait for database and role info to propagate across cluster
     */
    private void waitForDbAndRoleInfoToPropagate() throws InterruptedException {
        Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS * 4);
    }

    /**
     * Get connection to specific database with specific role through connection manager
     */
    private Connection getRouteConnection(String database, String role) throws Exception {
        return getConnectionBuilder().withDatabase(database).withUser(role)
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
    }

    /**
     * Get current GUC value from a statement
     */
    private String getGucValue(Statement stmt) throws SQLException {
        try (ResultSet rs = stmt.executeQuery("SHOW " + TEST_GUC)) {
            assertTrue("Expected result from SHOW " + TEST_GUC, rs.next());
            return rs.getString(1);
        }
    }

    /**
     * Generate ALTER statement based on type, using the specified raw value
     */
    private String generateAlterStatement(AlterType type, String rawValue) {
        switch (type) {
            case ROLE_ALL:
                return String.format("ALTER ROLE ALL SET %s = %s", TEST_GUC, rawValue);
            case DATABASE:
                return String.format("ALTER DATABASE %s SET %s = %s", TEST_DB1, TEST_GUC, rawValue);
            case ROLE:
                return String.format("ALTER ROLE %s SET %s = %s", TEST_ROLE1, TEST_GUC, rawValue);
            case ROLE_IN_DATABASE:
                return String.format("ALTER ROLE %s IN DATABASE %s SET %s = %s", TEST_ROLE1,
                        TEST_DB1, TEST_GUC, rawValue);
            default:
                throw new IllegalArgumentException("Unknown ALTER type: " + type);
        }
    }

    /**
     * Check if a connection route should be affected by the ALTER statement
     */
    private boolean isRouteAffected(AlterType type, String database, String role) {
        switch (type) {
            case ROLE_ALL:
                return true;
            case DATABASE:
                return TEST_DB1.equals(database);
            case ROLE:
                return TEST_ROLE1.equals(role);
            case ROLE_IN_DATABASE:
                return TEST_DB1.equals(database) && TEST_ROLE1.equals(role);
            default:
                return false;
        }
    }

    /**
     * Create connections to all 4 routes (db1/role1, db1/role2, db2/role1, db2/role2)
     */
    private List<ConnectionContext> createAllRouteConnections() throws Exception {
        List<ConnectionContext> contexts = new ArrayList<>();
        String[][] routes = {{TEST_DB1, TEST_ROLE1}, {TEST_DB1, TEST_ROLE2}, {TEST_DB2, TEST_ROLE1},
                {TEST_DB2, TEST_ROLE2}};

        for (String[] route : routes) {
            Connection conn = getRouteConnection(route[0], route[1]);
            Statement stmt = conn.createStatement();
            contexts.add(new ConnectionContext(conn, stmt, route[0], route[1]));
        }
        return contexts;
    }

    /**
     * Close all connection contexts
     */
    private void closeAllConnections(List<ConnectionContext> contexts) {
        for (ConnectionContext ctx : contexts) {
            ctx.close();
        }
    }

    /**
     * Verify GUC value on a connection matches expected value
     */
    private void verifyGucValue(ConnectionContext ctx, String expectedValue, String context)
            throws SQLException {
        String actualValue = getGucValue(ctx.statement);
        assertEquals(
                String.format("%s: Expected GUC value '%s' but got '%s' for route %s/%s", context,
                        expectedValue, actualValue, ctx.database, ctx.role),
                expectedValue, actualValue);
    }

    /**
     * Verify GUC value is one of the allowed values (for fluctuating strategy)
     */
    private void verifyGucValueOneOf(ConnectionContext ctx, String value1, String value2,
            String context) throws SQLException {
        String actualValue = getGucValue(ctx.statement);
        assertTrue(
                String.format(
                        "%s: Expected GUC value to be '%s' or '%s' but got '%s' for route %s/%s",
                        context, value1, value2, actualValue, ctx.database, ctx.role),
                value1.equals(actualValue) || value2.equals(actualValue));
    }

    // ==================== Main Test Logic ====================

    /**
     * Run all ALTER types in precedence order for a given strategy and TTL. This is the main test
     * method - cluster is restarted once at the beginning.
     */
    private void runAllAlterTypesInPrecedenceOrder(String strategy, int ttlMs) throws Exception {
        LOG.info("=== Testing strategy: {}, TTL: {} ===", strategy, ttlMs);

        // 1. Restart cluster ONCE with the specified flags
        restartClusterWithAlterGucFlags(strategy, ttlMs);

        // 2. Create test DBs and roles ONCE
        createTestDbsAndRoles();

        // Track expected values as we progress through ALTER types
        ExpectedValues expectedValues = new ExpectedValues();

        try {
            // 3. Test each ALTER type in precedence order (lowest to highest)
            for (AlterType alterType : PRECEDENCE_ORDER) {
                String newValue = GUC_VALUES[alterType.precedenceIndex];
                String newValueRaw = GUC_VALUES_RAW[alterType.precedenceIndex];

                testSingleAlterType(alterType, strategy, ttlMs, expectedValues, newValue,
                        newValueRaw);

                // Update expected values for next iteration
                expectedValues.updateForAlter(alterType, newValue);
            }
        } finally {
            cleanupTestDbsAndRoles();
        }
    }

    /**
     * Test a single ALTER type with the given strategy and TTL. Uses expectedValues to know what
     * values each route should currently have.
     */
    private void testSingleAlterType(AlterType alterType, String strategy, int ttlMs,
            ExpectedValues expectedBefore, String newValue, String newValueRaw) throws Exception {
        LOG.info("--- Testing ALTER type: {} (new value: {}) ---", alterType, newValue);

        List<ConnectionContext> preAlterConnections = null;
        try {
            // Create pre-ALTER connections to all routes
            preAlterConnections = createAllRouteConnections();

            // Verify all pre-ALTER connections see their expected GUC values
            for (ConnectionContext ctx : preAlterConnections) {
                String expectedOld = expectedBefore.getExpected(ctx.database, ctx.role);
                verifyGucValue(ctx, expectedOld, "Pre-ALTER " + alterType);
            }

            // Take a snapshot of expected values before ALTER (for comparison in verification)
            ExpectedValues snapshotBefore = expectedBefore.copy();

            // Execute the ALTER statement
            try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
                    Statement stmt = conn.createStatement()) {
                String alterSql = generateAlterStatement(alterType, newValueRaw);
                LOG.info("Executing: {}", alterSql);
                stmt.execute(alterSql);
            }

            // Verify new connections see correct values
            verifyNewConnections(alterType, strategy, snapshotBefore, newValue);

            // Verify behavior based on TTL
            if (ttlMs == TTL_NO_LIMIT) {
                // TTL=-1: backends never killed
                verifyTtlMinusOneBehavior(alterType, strategy, preAlterConnections, snapshotBefore,
                        newValue);
            } else if (ttlMs == TTL_IMMEDIATE) {
                // TTL=0: backends killed immediately
                Thread.sleep(1000); // Small wait for termination to complete
                verifyAfterTtlExpiry(alterType, strategy, ttlMs, preAlterConnections,
                        snapshotBefore, newValue);
            } else {
                // TTL>0: wait for TTL to expire, then verify
                LOG.info("Waiting for TTL ({} ms) to expire", ttlMs);
                Thread.sleep(ttlMs + 1000); // Add buffer for processing
                verifyAfterTtlExpiry(alterType, strategy, ttlMs, preAlterConnections,
                        snapshotBefore, newValue);
            }

        } finally {
            if (preAlterConnections != null) {
                closeAllConnections(preAlterConnections);
            }
        }
    }

    /**
     * Verify behavior when TTL=-1 (backends never killed)
     */
    private void verifyTtlMinusOneBehavior(AlterType alterType, String strategy,
            List<ConnectionContext> preAlterConnections, ExpectedValues snapshotBefore,
            String newValue) throws Exception {
        LOG.info("Waiting {} ms for TTL=-1 verification", WAIT_FOR_TTL_MINUS_1_MS);
        Thread.sleep(WAIT_FOR_TTL_MINUS_1_MS);

        LOG.info("Verifying existing connections after wait (strategy: {})", strategy);

        for (ConnectionContext ctx : preAlterConnections) {
            boolean isAffected = isRouteAffected(alterType, ctx.database, ctx.role);
            String oldValue = snapshotBefore.getExpected(ctx.database, ctx.role);

            if (!isAffected) {
                // Unaffected routes should always see their previous value
                verifyGucValue(ctx, oldValue, "TTL=-1 unaffected");
                continue;
            }

            // Affected routes - behavior depends on strategy
            switch (strategy) {
                case STRATEGY_FLUCTUATING:
                    // GUC may fluctuate - verify it's one of the two values
                    verifyGucValueOneOf(ctx, oldValue, newValue, "TTL=-1 fluctuating");
                    break;
                case STRATEGY_GRADUAL:
                    // Existing connections should adopt new values
                    verifyGucValue(ctx, newValue, "TTL=-1 gradual");
                    break;
                case STRATEGY_CONNECTION_STATIC:
                    // Existing connections retain OLD values
                    verifyGucValue(ctx, oldValue, "TTL=-1 connection_static");
                    break;
                default:
                    fail("Unknown strategy: " + strategy);
            }
        }
    }

    /**
     * Verify behavior after TTL expires (for TTL=0 or TTL>0)
     */
    private void verifyAfterTtlExpiry(AlterType alterType, String strategy, int ttlMs,
            List<ConnectionContext> preAlterConnections, ExpectedValues snapshotBefore,
            String newValue) throws Exception {
        LOG.info("Verifying connections after TTL expiry (strategy: {})", strategy);

        for (ConnectionContext ctx : preAlterConnections) {
            boolean isAffected = isRouteAffected(alterType, ctx.database, ctx.role);
            String oldValue = snapshotBefore.getExpected(ctx.database, ctx.role);

            if (!isAffected) {
                // Unaffected routes should still work and see their previous value
                try {
                    verifyGucValue(ctx, oldValue, "Post-TTL unaffected");
                } catch (SQLException e) {
                    // If connection failed, create new one and verify
                    try (Connection newConn = getRouteConnection(ctx.database, ctx.role);
                            Statement newStmt = newConn.createStatement()) {
                        String actualValue = getGucValue(newStmt);
                        assertEquals("Post-TTL unaffected (reconnected)", oldValue, actualValue);
                    }
                }
                continue;
            }

            // For affected routes with connection_static strategy and TTL>0,
            // existing connections should fail because their backends were destroyed
            if (STRATEGY_CONNECTION_STATIC.equals(strategy) && ttlMs > 0) {
                try {
                    ctx.statement.execute("SELECT 1");
                    fail(String
                            .format("Connection for %s/%s should have failed after TTL but backend"
                                    + " was not destroyed", ctx.database, ctx.role));
                } catch (SQLException e) {
                    // Expected - backend was destroyed after TTL
                    LOG.info("Connection for {}/{} correctly failed after backend destroyed: {}",
                            ctx.database, ctx.role, e.getMessage());
                }
            } else {
                // For other strategies, verify the connection sees new value
                try {
                    String actualValue = getGucValue(ctx.statement);
                    if (STRATEGY_FLUCTUATING.equals(strategy)) {
                        assertTrue(
                                String.format(
                                        "Post-TTL fluctuating for %s/%s: expected '%s'"
                                                + " or '%s' but got '%s'",
                                        ctx.database, ctx.role, oldValue, newValue, actualValue),
                                oldValue.equals(actualValue) || newValue.equals(actualValue));
                    } else {
                        assertEquals(String.format("Post-TTL for %s/%s", ctx.database, ctx.role),
                                newValue, actualValue);
                    }
                } catch (SQLException e) {
                    // Connection may have been terminated - create new one
                    LOG.info("Existing connection for {}/{} was terminated, "
                            + "verifying with new connection", ctx.database, ctx.role);
                    try (Connection newConn = getRouteConnection(ctx.database, ctx.role);
                            Statement newStmt = newConn.createStatement()) {
                        String actualValue = getGucValue(newStmt);
                        assertEquals(String.format("Post-TTL new conn for %s/%s", ctx.database,
                                ctx.role), newValue, actualValue);
                    }
                }
            }
        }
    }

    /**
     * Verify new connections see correct values after ALTER
     */
    private void verifyNewConnections(AlterType alterType, String strategy,
            ExpectedValues snapshotBefore, String newValue) throws Exception {
        LOG.info("Verifying new connections after ALTER");

        String[][] routes = {{TEST_DB1, TEST_ROLE1}, {TEST_DB1, TEST_ROLE2}, {TEST_DB2, TEST_ROLE1},
                {TEST_DB2, TEST_ROLE2}};

        for (String[] route : routes) {
            try (Connection conn = getRouteConnection(route[0], route[1]);
                    Statement stmt = conn.createStatement()) {
                String actualValue = getGucValue(stmt);
                boolean isAffected = isRouteAffected(alterType, route[0], route[1]);
                String oldValue = snapshotBefore.getExpected(route[0], route[1]);

                if (!isAffected) {
                    // Unaffected routes should see their previous value
                    assertEquals(String.format("New conn for unaffected %s/%s", route[0], route[1]),
                            oldValue, actualValue);
                } else if (STRATEGY_FLUCTUATING.equals(strategy)) {
                    // Fluctuating: new connections may see old or new value
                    assertTrue(String.format(
                            "New conn (fluctuating) for %s/%s: expected '%s' or '%s' but got '%s'",
                            route[0], route[1], oldValue, newValue, actualValue),
                            oldValue.equals(actualValue) || newValue.equals(actualValue));
                } else {
                    // Gradual/connection_static: new connections must see new value
                    assertEquals(String.format("New conn for %s/%s", route[0], route[1]), newValue,
                            actualValue);
                }
            }
        }
    }

    // ==================== Test Methods ====================
    // 6 meaningful (strategy, TTL) combinations instead of 9

    /**
     * TTL=0 (immediate): Only one strategy needed since backends die immediately. Strategy is
     * irrelevant when backends are killed right away.
     */
    @Test
    public void testTtlImmediateFluctuating() throws Exception {
        runAllAlterTypesInPrecedenceOrder(STRATEGY_FLUCTUATING, TTL_IMMEDIATE);
    }

    /**
     * TTL=-1 (no limit) with connection_static: PG-like behavior where existing connections retain
     * their original values.
     */
    @Test
    public void testTtlNoLimitConnectionStatic() throws Exception {
        runAllAlterTypesInPrecedenceOrder(STRATEGY_CONNECTION_STATIC, TTL_NO_LIMIT);
    }

    /**
     * TTL=5s with fluctuating: Test TTL expiry with fluctuating values.
     */
    @Test
    public void testTtl5SecondsFluctuating() throws Exception {
        runAllAlterTypesInPrecedenceOrder(STRATEGY_FLUCTUATING, TTL_5_SECONDS);
    }

    /**
     * TTL=5s with gradual: Test TTL expiry with gradual adoption of new values.
     */
    @Test
    public void testTtl5SecondsGradual() throws Exception {
        runAllAlterTypesInPrecedenceOrder(STRATEGY_GRADUAL, TTL_5_SECONDS);
    }

    /**
     * TTL=5s with connection_static: Test TTL expiry and verify existing connections fail after
     * their backends are destroyed.
     */
    @Test
    public void testTtl5SecondsConnectionStatic() throws Exception {
        runAllAlterTypesInPrecedenceOrder(STRATEGY_CONNECTION_STATIC, TTL_5_SECONDS);
    }
}
