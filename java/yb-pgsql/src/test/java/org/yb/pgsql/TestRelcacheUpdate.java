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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.Statement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.net.URL;
import java.net.URLConnection;
import java.util.Arrays;
import java.util.Scanner;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.IntStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.function.BiConsumer;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.Map;
import java.util.HashMap;
import java.util.Properties;
import java.util.Queue;
import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.Files;
import org.yb.util.BuildTypeUtil;
import org.yb.util.SystemUtil;

import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;

/**
 * Tests that the relcache update optimizations work as expected. The optimized relcache update is
 * used in the following cases: 1. During connection startup a. On the first connection after a DDL
 * or after cluster startup, or b. Every connection, if
 * ysql_catalog_preload_additional_tables(_list) is enabled. 2. During cache refresh.
 *
 * In this test, we force the relcache to be updated during connection startup by calling
 * invalidateRelcache(), which does an ALTER TABLE DDL.
 */
@RunWith(value = YBTestRunner.class)
public class TestRelcacheUpdate extends BasePgSQLTest {
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_pg_conf_csv", "log_statement=all");
    return flags;
  }

  private static final Logger LOG = LoggerFactory.getLogger(TestRelcacheUpdate.class);

  /**
   * Invalidates the relcache by doing a DDL that increments the catalog version and waits for it to
   * be propagated.
   */
  private void invalidateRelcache() throws SQLException, InterruptedException {
    connection.createStatement()
        .execute("CREATE TABLE test_relcache_update (id INT PRIMARY KEY);");
    connection.createStatement().execute("ALTER TABLE test_relcache_update ADD COLUMN data TEXT;");
    connection.createStatement().execute("DROP TABLE test_relcache_update;");
    Thread.sleep(2 * MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS);
  }

  /**
   * Gets memory stats from /proc/<pid>/status for a given process ID.
   *
   * @param pid Process ID to get memory stats for
   * @return Map containing VmHWM and VmRSS values in KB, or null if stats cannot be read
   */
  private Map<String, Long> getMemoryStats(int pid) throws IOException {
    Path procPath = Paths.get("/proc/" + pid + "/status");
    Map<String, Long> stats = new HashMap<>();

    for (String line : Files.readAllLines(procPath)) {
      if (line.startsWith("VmHWM:")) {
        stats.put("VmHWM", Long.parseLong(line.split("\\s+")[1])); // Value in KB
      } else if (line.startsWith("VmRSS:")) {
        stats.put("VmRSS", Long.parseLong(line.split("\\s+")[1])); // Value in KB
      }
    }

    return stats;
  }

  private long getVmRSS(int pid) throws IOException {
    Map<String, Long> memStats = getMemoryStats(pid);
    return memStats.get("VmRSS");
  }

  /**
   * Checks that a given backend process never used more than 50MB above its idle memory usage.
   *
   * @param stmt Statement to execute
   * @throws Exception if memory spike exceeds threshold or statement execution fails
   */
  private void checkForMemorySpike(Statement stmt) throws Exception {
    // Skip this check on Mac since /proc is not available
    if (SystemUtil.IS_MAC) {
      return;
    }

    ResultSet pidRs = stmt.executeQuery("SELECT pg_backend_pid()");
    pidRs.next();
    int pid = pidRs.getInt(1);

    Map<String, Long> memStats = getMemoryStats(pid);
    long peakMemoryKB = memStats.get("VmHWM");
    long currentMemoryKB = memStats.get("VmRSS");

    long diff = peakMemoryKB - currentMemoryKB;
    assertTrue(String.format("Memory spike of %d KB exceeds 50 MB", diff), diff < 50 * 1024);
  }

  private int getCurrentDatabaseOid(final Connection inputConnection) throws Exception {
    try (Statement statement = inputConnection.createStatement()) {
      ResultSet result = statement.executeQuery(
        "SELECT oid FROM pg_database WHERE datname = current_database()");
      assertTrue(result.next());
      return result.getInt("oid");
    }
  }

  private String getCatalogVersions(Statement stmt) throws Exception {
    ResultSet resultSet = stmt.executeQuery(
      "SELECT db_oid, current_version FROM pg_yb_catalog_version");
    Set<Row> rows = getRowSet(resultSet);
    return rows.toString();
  }

  @Test
  public void testTriggerBasic() throws Exception {
    // Number of triggers to create.
    final int numTriggers = 5;

    try (Statement stmt = connection.createStatement()) {
      // Create tables with foreign keys
      String[] createTableStmts = {
          "CREATE TABLE parent (id INT PRIMARY KEY, data TEXT);", "CREATE TABLE child ("
              + "id INT PRIMARY KEY, " + "parent_id INT REFERENCES parent(id), " + "data TEXT);",
          "CREATE TABLE trigger_log (id SERIAL PRIMARY KEY, message TEXT);"};
      for (String sql : createTableStmts) {
        stmt.execute(sql);
      }
      stmt.execute("INSERT INTO parent (id, data) VALUES (1, 'parent data');");
      stmt.execute("INSERT INTO child (id, parent_id, data) VALUES (1, 1, 'child data');");

      invalidateRelcache();

      // Attempt to insert into child with invalid parent_id (should fail)
      try (Connection newConn = getConnectionBuilder().connect()) {
        Statement newStmt = newConn.createStatement();
        newStmt.execute(
            "INSERT INTO child (id, parent_id, data) VALUES (2, 999, 'orphan child data');");
        fail("Expected foreign key violation exception");
      } catch (SQLException e) {
        assertEquals("23503", e.getSQLState());
      }

      // Create trigger functions
      String triggerFuncTemplate =
          "CREATE OR REPLACE FUNCTION parent_trigger_function%s() RETURNS trigger AS $$ "
              + "BEGIN "
              + "INSERT INTO trigger_log (message) VALUES ('Trigger %s fired on parent table'); "
              + "RETURN NEW; " + "END; " + "$$ LANGUAGE plpgsql;";

      List<String> triggerFuncs = new ArrayList<>();
      List<String> triggers = new ArrayList<>();

      for (int i = 0; i < numTriggers; i++) {
        int triggerNum = i + 1;
        triggerFuncs.add(String.format(triggerFuncTemplate, triggerNum, triggerNum));
        triggers.add(String.format(
            "CREATE TRIGGER parent_trigger_%d AFTER INSERT ON parent "
                + "FOR EACH ROW EXECUTE FUNCTION parent_trigger_function%s();",
            triggerNum, triggerNum));
      }

      // Execute all trigger function creation statements
      for (String func : triggerFuncs) {
        stmt.execute(func);
      }

      // Create all triggers
      for (String trigger : triggers) {
        stmt.execute(trigger);
      }

      // Helper function to clear log and insert test data
      BiConsumer<Integer, String> clearAndInsert = (id, data) -> {
        try {
          stmt.execute("DELETE FROM trigger_log;");
          stmt.execute(
              String.format("INSERT INTO parent (id, data) VALUES (%d, '%s');", id, data));
        } catch (SQLException e) {
          throw new RuntimeException(e);
        }
      };

      invalidateRelcache();

      // Test both triggers firing
      try (Connection newConn = getConnectionBuilder().connect()) {
        Statement newStmt = newConn.createStatement();
        newStmt.execute("DELETE FROM trigger_log;");
        newStmt.execute("INSERT INTO parent (id, data) VALUES (2, 'parent data with triggers');");

        ResultSet rs = newStmt.executeQuery("SELECT message FROM trigger_log ORDER BY id;");
        ArrayList<String> actualMessages = new ArrayList<String>();
        while (rs.next()) {
          actualMessages.add(rs.getString("message"));
        }

        // We expect each trigger to fire exactly once.
        ArrayList<String> expectedMessages = new ArrayList<String>();
        IntStream.rangeClosed(1, numTriggers)
            .mapToObj(i -> String.format("Trigger %d fired on parent table", i))
            .forEach(expectedMessages::add);

        assertEquals(numTriggers, actualMessages.size());
        assertTrue(actualMessages.containsAll(expectedMessages));
        assertTrue(expectedMessages.containsAll(actualMessages));
      }

      // Test dropping first trigger
      stmt.execute("DROP TRIGGER parent_trigger_1 ON parent;");

      invalidateRelcache();

      try (Connection newConn = getConnectionBuilder().connect()) {
        Statement newStmt = newConn.createStatement();
        newStmt.execute("DELETE FROM trigger_log;");
        newStmt.execute("INSERT INTO parent (id, data) VALUES "
            + "(3, 'parent data after dropping first trigger');");

        ResultSet rs = newStmt.executeQuery("SELECT message FROM trigger_log ORDER BY id;");
        for (int i = 2; i <= numTriggers; i++) {
          assertTrue(rs.next());
          assertEquals(String.format("Trigger %d fired on parent table", i),
              rs.getString("message"));
        }
        assertFalse(rs.next());
      }

      // Test dropping the remaining triggers
      for (int i = 2; i <= numTriggers; i++) {
        stmt.execute(String.format("DROP TRIGGER parent_trigger_%d ON parent;", i));
      }

      invalidateRelcache();

      try (Connection newConn = getConnectionBuilder().connect()) {
        invalidateRelcache();
        Statement newStmt = newConn.createStatement();
        newStmt.execute("DELETE FROM trigger_log;");
        newStmt.execute("INSERT INTO parent (id, data) VALUES "
            + "(4, 'parent data after dropping all triggers');");

        ResultSet rs = newStmt.executeQuery("SELECT COUNT(*) FROM trigger_log;");
        assertTrue(rs.next());
        assertEquals(0, rs.getInt(1));
      }
    }
  }

  @Test
  public void testTriggerExecutionOrder() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Create tables
      String[] createTableStmts = {"CREATE TABLE order_test (id INT PRIMARY KEY, data TEXT);",
          "CREATE TABLE trigger_order_log (id SERIAL PRIMARY KEY, trigger_name TEXT, "
              + "execution_order INT);"};
      for (String sql : createTableStmts) {
        stmt.execute(sql);
      }

      // Create trigger functions
      String triggerFuncTemplate =
          "CREATE OR REPLACE FUNCTION trigger_func_%s() RETURNS trigger AS $$ " + "BEGIN "
              + "INSERT INTO trigger_order_log (trigger_name) VALUES ('trigger_%s'); "
              + "RETURN NEW; " + "END; " + "$$ LANGUAGE plpgsql;";

      String createTriggerTemplate = "CREATE TRIGGER trigger_%s " + "AFTER INSERT ON order_test "
          + "FOR EACH ROW EXECUTE FUNCTION trigger_func_%s();";

      String[] triggerNames = {"a", "b", "c"};

      // Create functions and triggers
      for (String name : triggerNames) {
        stmt.execute(String.format(triggerFuncTemplate, name, name));
        stmt.execute(String.format(createTriggerTemplate, name, name));
      }
      // Helper function to test trigger execution order
      Runnable testTriggerOrder = () -> {
        try {
          stmt.execute("DELETE FROM trigger_order_log;");
          stmt.execute("DELETE FROM order_test;");
          stmt.execute("INSERT INTO order_test (id, data) VALUES (1, 'test data');");

          ResultSet rs =
              stmt.executeQuery("SELECT trigger_name FROM trigger_order_log ORDER BY id;");

          // Postgres dictates that triggers are executed in alphabetical order.
          // https://www.postgresql.org/docs/15/sql-createtrigger.html
          List<String> expectedOrder = Arrays.asList("trigger_a", "trigger_b", "trigger_c");
          int index = 0;
          while (rs.next()) {
            assertEquals(expectedOrder.get(index), rs.getString("trigger_name"));
            index++;
          }
          assertEquals(expectedOrder.size(), index);
        } catch (SQLException e) {
          throw new RuntimeException(e);
        }
      };

      // Test initial order
      testTriggerOrder.run();

      // Drop and recreate triggers in reverse order
      for (String name : triggerNames) {
        stmt.execute(String.format("DROP TRIGGER trigger_%s ON order_test;", name));
      }

      for (int i = triggerNames.length - 1; i >= 0; i--) {
        stmt.execute(String.format(createTriggerTemplate, triggerNames[i], triggerNames[i]));
      }

      // Test order after recreation
      testTriggerOrder.run();
    }
  }

  @Test
  public void testRowLevelSecurityBasic() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Create roles
      stmt.execute("CREATE ROLE alice LOGIN PASSWORD 'alicepass';");
      stmt.execute("CREATE ROLE bob LOGIN PASSWORD 'bobpass';");

      // Create table
      stmt.execute("CREATE TABLE confidential_data (id INT PRIMARY KEY, owner TEXT, data TEXT);");

      // Insert data
      stmt.execute("INSERT INTO confidential_data (id, owner, data) VALUES "
          + "(1, 'alice', 'Alice''s secret'), " + "(2, 'bob', 'Bob''s secret'), "
          + "(3, 'carol', 'Carol''s secret');");

      // Enable RLS
      stmt.execute("ALTER TABLE confidential_data ENABLE ROW LEVEL SECURITY;");
      stmt.execute("GRANT SELECT ON confidential_data TO public;");

      // Create policy: users can see their own data
      stmt.execute("CREATE POLICY owner_select_policy ON confidential_data "
          + "FOR SELECT USING (owner = CURRENT_USER);");

      invalidateRelcache();

      // Test as Alice
      try (Connection aliceConn =
          getConnectionBuilder().withUser("alice").withPassword("alicepass").connect()) {
        Statement aliceStmt = aliceConn.createStatement();
        ResultSet rs = aliceStmt.executeQuery("SELECT data FROM confidential_data;");
        List<String> aliceData = new ArrayList<>();
        while (rs.next()) {
          aliceData.add(rs.getString("data"));
        }
        assertEquals(1, aliceData.size());
        assertTrue(aliceData.contains("Alice's secret"));
      }

      invalidateRelcache();

      // Test as Bob
      try (Connection bobConn =
          getConnectionBuilder().withUser("bob").withPassword("bobpass").connect()) {
        Statement bobStmt = bobConn.createStatement();
        ResultSet rs = bobStmt.executeQuery("SELECT data FROM confidential_data;");
        List<String> bobData = new ArrayList<>();
        while (rs.next()) {
          bobData.add(rs.getString("data"));
        }
        assertEquals(1, bobData.size());
        assertTrue(bobData.contains("Bob's secret"));
      }

      invalidateRelcache();

      // Test as a newly-created user
      stmt.execute("CREATE ROLE carol LOGIN PASSWORD 'carolpass';");
      try (Connection carolConn =
          getConnectionBuilder().withUser("carol").withPassword("carolpass").connect()) {
        Statement carolStmt = carolConn.createStatement();
        ResultSet rs = carolStmt.executeQuery("SELECT data FROM confidential_data;");
        List<String> carolData = new ArrayList<>();
        while (rs.next()) {
          carolData.add(rs.getString("data"));
        }
        assertEquals(1, carolData.size());
        assertTrue(carolData.contains("Carol's secret"));
      }

      invalidateRelcache();

      // Test as a superuser
      stmt.execute("CREATE ROLE admin LOGIN PASSWORD 'adminpass' SUPERUSER;");
      try (Connection adminConn =
          getConnectionBuilder().withUser("admin").withPassword("adminpass").connect()) {
        Statement adminStmt = adminConn.createStatement();
        ResultSet rs = adminStmt.executeQuery("SELECT data FROM confidential_data;");
        List<String> adminData = new ArrayList<>();
        while (rs.next()) {
          adminData.add(rs.getString("data"));
        }
        assertEquals(3, adminData.size());
      }
    }
  }

  @Test
  public void testRowLevelSecurityInsertPolicy() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Create roles
      stmt.execute("CREATE ROLE dave LOGIN PASSWORD 'davepass';");

      // Create table
      stmt.execute("CREATE TABLE messages (" + "id SERIAL PRIMARY KEY, " + "sender TEXT, "
          + "recipient TEXT, " + "message TEXT);");

      // Enable RLS
      stmt.execute("ALTER TABLE messages ENABLE ROW LEVEL SECURITY;");

      // Create policy: users can insert messages only if they are the sender
      stmt.execute("CREATE POLICY insert_policy ON messages "
          + "FOR INSERT WITH CHECK (sender = CURRENT_USER);");

      // Grant INSERT privilege
      stmt.execute("GRANT ALL ON messages TO public;");

      // We also need to grant USAGE and SELECT on the sequence for the id column to be
      // able to insert into the table.
      stmt.execute("GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO public;");

      invalidateRelcache();

      // Test valid insert
      try (Connection daveConn =
          getConnectionBuilder().withUser("dave").withPassword("davepass").connect()) {
        Statement daveStmt = daveConn.createStatement();
        int rowsInserted =
            daveStmt.executeUpdate("INSERT INTO messages (sender, recipient, message) VALUES "
                + "('dave', 'eve', 'Hello Eve');");
        assertEquals(1, rowsInserted);
      }

      invalidateRelcache();

      // Test invalid insert
      try (Connection daveConn =
          getConnectionBuilder().withUser("dave").withPassword("davepass").connect()) {
        Statement daveStmt = daveConn.createStatement();
        daveStmt.executeUpdate("INSERT INTO messages (sender, recipient, message) VALUES "
            + "('mallory', 'eve', 'Intrusion');");
        fail("Expected an exception for violating RLS policy");
      } catch (SQLException e) {
        // Expecting a SQL exception due to RLS policy violation
        assertEquals("42501", e.getSQLState()); // insufficient privilege
      }
    }
  }

  @Test
  public void testRowLevelSecurityUpdatePolicy() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Create roles
      stmt.execute("CREATE ROLE editor LOGIN PASSWORD 'editorpass';");
      stmt.execute("CREATE ROLE viewer LOGIN PASSWORD 'viewerpass';");

      // Create table
      stmt.execute("CREATE TABLE articles (id INT PRIMARY KEY, author TEXT, content TEXT);");

      // Insert data
      stmt.execute("INSERT INTO articles (id, author, content) VALUES "
          + "(1, 'editor', 'Initial Content');");

      // Enable RLS
      stmt.execute("ALTER TABLE articles ENABLE ROW LEVEL SECURITY;");

      // Create policies
      stmt.execute("CREATE POLICY select_policy ON articles " + "FOR SELECT USING (true);");
      stmt.execute("CREATE POLICY update_policy ON articles "
          + "FOR UPDATE USING (author = CURRENT_USER);");

      // Grant privileges
      stmt.execute("GRANT SELECT ON articles TO viewer;");
      stmt.execute("GRANT SELECT, UPDATE ON articles TO editor;");

      invalidateRelcache();

      // Test update as editor
      try (Connection editorConn =
          getConnectionBuilder().withUser("editor").withPassword("editorpass").connect()) {
        Statement editorStmt = editorConn.createStatement();
        int rowsUpdated = editorStmt
            .executeUpdate("UPDATE articles SET content = 'Updated Content' WHERE id = 1;");
        assertEquals(1, rowsUpdated);
      }

      invalidateRelcache();

      // Test update as viewer
      try (Connection viewerConn =
          getConnectionBuilder().withUser("viewer").withPassword("viewerpass").connect()) {
        Statement viewerStmt = viewerConn.createStatement();
        viewerStmt.executeUpdate("UPDATE articles SET content = 'Hacked Content' WHERE id = 1;");
        fail("Expected an exception for violating RLS policy");
      } catch (SQLException e) {
        // Expecting a SQL exception due to RLS policy violation
        assertEquals("42501", e.getSQLState());
      }
    }
  }

  /**
   * Tests that several connections can refresh their cache concurrently when there are a lot of
   * tables.
   *
   * @throws Exception
   */
  @Test
  public void testConcurrentCacheRefresh() throws Exception {
    // Create 300 tables, each with 35 columns
    // For sanitizer builds, only create 50 tables to avoid timeouts.
    boolean isSanitizerBuild = BuildTypeUtil.isASAN() || BuildTypeUtil.isTSAN();
    int numTables = isSanitizerBuild ? 50 : 300;
    try (Statement stmt = connection.createStatement()) {
      String[] columnDefs = {"id", "uuid_col", "name", "c", "info", "contact", "arr", "cash", "i",
          "m", "i2", "i3", "val", "details", "age", "collated_data", "date", "n", "r", "c1",
          "created_at", "uuid0", "uuid1", "p1", "t1", "ts1", "i4", "p2", "p3", "b", "c2", "l",
          "l1", "a2", "zip"};

      String tableColumns = Arrays.stream(columnDefs).map(col -> col + " int default 10")
          .reduce((a, b) -> a + ", " + b).get();

      for (int i = 1; i <= numTables; i++) {
        String tableName = String.format("c_table_%d", i);
        LOG.info("[{}/{}] Creating table {}", i, numTables, tableName);
        stmt.execute(String.format("CREATE TABLE IF NOT EXISTS %s (%s)", tableName, tableColumns));
      }
    }

    // Create 5 connections
    int numConnections = 5;
    Connection[] connections = new Connection[numConnections];
    for (int i = 0; i < numConnections; i++) {
      connections[i] = getConnectionBuilder().connect();
    }

    invalidateRelcache();

    // Create threads to execute SELECT queries in parallel
    Thread[] threads = new Thread[numConnections];
    final CountDownLatch latch = new CountDownLatch(1);

    List<Exception> exceptions = new ArrayList<>();
    for (int i = 0; i < numConnections; i++) {
      final int connIndex = i;
      threads[i] = new Thread(() -> {
        try {
          latch.await(); // Wait for all threads to be ready
          Statement connStmt = connections[connIndex].createStatement();

          // Run a query to trigger a cache refresh
          connStmt.executeQuery("SELECT * FROM c_table_1 LIMIT 1");

          checkForMemorySpike(connStmt);
        } catch (Exception e) {
          synchronized (exceptions) {
            exceptions.add(e);
          }
        }
      });
      threads[i].start();
    }

    // Start all threads simultaneously
    latch.countDown();

    // Wait for all threads to complete
    for (Thread thread : threads) {
      thread.join();
    }

    // If any exceptions occurred, throw them all
    if (!exceptions.isEmpty()) {
      RuntimeException combinedException =
          new RuntimeException("Exception(s) occurred while running parallel SELECT queries");
      for (Exception e : exceptions) {
        combinedException.addSuppressed(e);
      }
      throw combinedException;
    }
  }

  @Test
  public void testRelcacheInitConnectionStress() throws Exception {
    skipYsqlConnMgr(BasePgSQLTest.RELCACHE_INIT_NEEDS_NEW_BACKEND,
                isTestRunningWithConnectionManager());
    boolean isSanitizerBuild = BuildTypeUtil.isASAN() || BuildTypeUtil.isTSAN();
    // Number of databases and connections per DB.
    final int NUM_DATABASES = isSanitizerBuild ? 2 : 10;
    final int CONNECTIONS_PER_DB = isSanitizerBuild ? 5 : 20;
    final String TEST_USER = "test_user";

    // --- Setup ---
    List<String> dbNames = new ArrayList<>();
    for (int i = 0; i < NUM_DATABASES; i++) {
      dbNames.add("test_db_" + i);
    }
    int totalConnections = NUM_DATABASES * CONNECTIONS_PER_DB;
    ExecutorService executorService = Executors.newCachedThreadPool();
    AtomicInteger numSuccesses = new AtomicInteger(0);
    AtomicInteger numFailures = new AtomicInteger(0);
    AtomicBoolean stopBumper = new AtomicBoolean(false);
    final Queue<Connection> establishedConnections = new ConcurrentLinkedQueue<>();

    try (Connection connSuperuser = getConnectionBuilder().connect();
         Statement stmtSuperuser = connSuperuser.createStatement()) {

      LOG.info("Starting catalog versions: {}", getCatalogVersions(stmtSuperuser));
      LOG.info("Creating test user: {}", TEST_USER);
      stmtSuperuser.execute(String.format("CREATE USER %s", TEST_USER));

      LOG.info("Creating {} databases...", NUM_DATABASES);
      for (String dbName : dbNames) {
        stmtSuperuser.execute(String.format("CREATE DATABASE %s", dbName));
      }

      executorService.submit(() -> {
        LOG.info("Catalog version bumper thread started.");
        // Use a separate connection/statement inside the thread for safety
        try (Connection bumperConn = getConnectionBuilder().connect();
             Statement bumperStmt = bumperConn.createStatement()) {
          int currentDatabaeOid = getCurrentDatabaseOid(bumperConn);
          // Continuously bump up catalog version to simulate burst of DDLs.
          while (!stopBumper.get()) {
            try {
              bumperStmt.execute(
                "SET yb_non_ddl_txn_for_sys_tables_allowed=1;" +
                "SELECT yb_increment_all_db_catalog_versions_with_inval_messages(" +
                currentDatabaeOid + ", false, '', 10);" +
                "SET yb_non_ddl_txn_for_sys_tables_allowed=0");
            } catch (Exception e) {
              if (!stopBumper.get()) {
                LOG.error("Error bumping catalog version: {}", e.getMessage());
              }
            }
          }
        } catch (Exception connEx) { // Catch errors getting the connection/statement
          LOG.error("Failed to set up connection for bumper thread: {}", connEx.getMessage());
        }
        LOG.info("Catalog version bumper thread stopped.");
      });

      // Latch to wait until all connection attempts are finished
      CountDownLatch connectionLatch = new CountDownLatch(totalConnections);

      // --- Submit Connection Tasks ---
      LOG.info("Submitting {} concurrent connection tasks...", totalConnections);
      Properties props = new Properties();
      if (isSanitizerBuild) {
        // Set to 60 second timeout for slower builds.
        props.setProperty("sslResponseTimeout", "60000");
        props.setProperty("loginTimeout", "120");
        props.setProperty("socketTimeout", "120");
      }
      for (String dbName : dbNames) {
        for (int j = 0; j < CONNECTIONS_PER_DB; j++) {
          final String currentDbName = dbName; // Need final variable for lambda
          executorService.submit(() -> {
            Connection conn = null; // Declare connection outside try
            try {
              // Try connecting as the test user to the specific database
              conn = getConnectionBuilder().withUser(TEST_USER)
                                           .withDatabase(currentDbName)
                                           .connect(props);
              // If successful, add to the queue
              establishedConnections.offer(conn); // Use offer for non-blocking add
              numSuccesses.incrementAndGet();
              LOG.debug("Successfully connected to {}", currentDbName);
            } catch (Exception e) {
              numFailures.incrementAndGet();
              LOG.warn("Failed to connect to {}: {}", currentDbName, e.getMessage());
            } finally {
              connectionLatch.countDown(); // Signal that this attempt is done
            }
          });
        }
      }

      // --- Wait for Completion ---
      LOG.info("Waiting for all {} connection attempts to complete...", totalConnections);
      // Wait indefinitely until all connection threads have finished
      connectionLatch.await();
      LOG.info("All connection attempts finished. Successes: {}, Failures: {}",
               numSuccesses.get(), numFailures.get());

      // --- Shutdown ---
      LOG.info("Stopping catalog version bumper...");
      stopBumper.set(true); // Signal the bumper thread to stop

      LOG.info("Shutting down executor service...");
      executorService.shutdown(); // Disable new tasks from being submitted
      if (!executorService.awaitTermination(60, TimeUnit.SECONDS)) { // Wait for existing tasks
        LOG.warn("Executor service did not terminate gracefully after 60 seconds.");
        executorService.shutdownNow(); // Force shutdown
      }
      LOG.info("Executor service shut down.");

      assertEquals("Total connection successes mismatch", totalConnections, numSuccesses.get());

      LOG.info("Ending catalog versions: {}", getCatalogVersions(stmtSuperuser));
    }

    LOG.info("Processing {} established connections...", establishedConnections.size());
    long minRSS = -1;
    long maxRSS = -1;
    long totalRSS = 0;
    long count = 0;
    for (Connection c : establishedConnections) {
      try {
        // If a connection is idled too long, jdbc can close it.
        if (c == null || c.isClosed()) {
          LOG.warn("Connection was closed or is null, skipping RSS check for this backend.");
          continue;
        }
        count++;
        Statement s = c.createStatement();
        ResultSet pidRs = s.executeQuery("SELECT pg_backend_pid()");
        pidRs.next();
        int pid = pidRs.getInt(1);
        long rss = getVmRSS(pid);
        if (minRSS == -1) {
          minRSS = rss;
        }
        if (maxRSS == -1) {
          maxRSS = rss;
        }
        if (rss < minRSS) {
          minRSS = rss;
        }
        if (rss > maxRSS) {
          maxRSS = rss;
        }
        LOG.info("PG backend with pid {} has RSS {}", pid, rss);
        totalRSS += rss;
      } catch (SQLException e) {
        LOG.warn("Error checking connection status: {}", e.getMessage());
      }
    }
    double avgRSS = (double)totalRSS / count;
    LOG.info("minRSS {}, maxRSS {}", minRSS, maxRSS);
    LOG.info("count {}, totalRSS {}, avgRSS {}", count, totalRSS, avgRSS);
    // Sanitizer build types take up a lot more overall memory. We do different
    // types of assertion for santizier builds and non-santizier builds.
    if (isSanitizerBuild) {
      // Assert the variations between connections are less than 60% for ASAN
      // and 30% for TSAN.
      double maxVariationPercent = (double)(maxRSS - minRSS) / minRSS;
      double expectedPercent = BuildTypeUtil.isASAN() ? 0.60 : 0.30;
      LOG.info("maxVariationPercent {}", maxVariationPercent);
      assertTrue(String.format("Expected maxVariationPercent less than %.2f%%, but was %.2f%%",
                               expectedPercent, maxVariationPercent),
                 maxVariationPercent < expectedPercent);
    } else {
      // Assert the absolute variation between connections is less than 30 MB.
      long maxVariationKB = maxRSS - minRSS;
      LOG.info("maxVariationKB {}", maxVariationKB);
      assertTrue(String.format("Expected maxVariationKB less than 30 MB, but was %d KB",
                               maxVariationKB),
                 maxVariationKB < 30 * 1024);
    }
  }

  /**
   * Confines the master processes to a single CPU, leaving the tservers and postgres backends the
   * whole machine.  num_cpus only changes what a process believes it has, which resizes thread
   * pools but leaves the scheduler free to run those threads on every core; this is what actually
   * starves the leader.  That asymmetry is the incident's: one master serving two hundred nodes,
   * each of which had a machine to itself.
   *
   * Linux only, and best effort -- a machine without taskset, or one where the call is refused,
   * leaves the masters unconfined and the run simply carries on without the handicap.
   */
  private void confineMastersToOneCpu() {
    for (MiniYBDaemon master : miniCluster.getMasters().values()) {
      try {
        int pid = master.getPid();
        Process p = new ProcessBuilder("taskset", "-acp", "0", String.valueOf(pid))
            .redirectErrorStream(true).start();
        if (p.waitFor() == 0) {
          LOG.info("Confined master pid {} to cpu 0", pid);
        } else {
          LOG.warn("Could not confine master pid {} to cpu 0; running unconfined", pid);
        }
      } catch (Exception e) {
        LOG.warn("Could not confine a master to cpu 0, running unconfined: {}", e.getMessage());
      }
    }
  }

  /**
   * Scales the fan-in for the stress test below.  Each tserver keeps its own catalog response
   * cache, so what reaches the master leader is one prefetch per catalog version per tserver --
   * the incident's multiplier was two hundred nodes, and this is the part of it a single machine
   * can turn up.  -1 leaves the default, which is what every other test in this class gets.
   */
  @Override
  protected int getInitialNumTServers() {
    return intFromEnv("YB_STRESS_TSERVERS", -1);
  }

  /** Reads a sizing knob from the environment so the stress test can be scaled to the machine. */
  private static int intFromEnv(String name, int defaultValue) {
    String value = System.getenv(name);
    return value == null || value.isEmpty() ? defaultValue : Integer.parseInt(value);
  }

  /** Sums the ysql_catalog_prefetch_* values the master exports, across all masters. */
  private long getMasterPrefetchMetric(String metricName) throws Exception {
    long total = 0;
    for (URL url : getMasterMetricSources()) {
      URLConnection connection = url.openConnection();
      connection.setUseCaches(false);
      try (Scanner scanner = new Scanner(connection.getInputStream(), "UTF-8")) {
        String body = scanner.useDelimiter("\\A").hasNext() ? scanner.next() : "";
        Matcher matcher = Pattern.compile(
            "\\{[^{}]*\"name\"\\s*:\\s*\"" + metricName + "\"[^{}]*\"value\"\\s*:\\s*(\\d+)")
            .matcher(body);
        while (matcher.find()) {
          total += Long.parseLong(matcher.group(1));
        }
      }
    }
    return total;
  }

  /**
   * Drives the catalog prefetch fan-in that motivated the master admission bound and the DDL
   * pacing built on it (issue #34309).  Every connection preloads the whole catalog, because
   * ysql_catalog_preload_additional_tables makes YbCatalogPreloadRequired() true and so skips the
   * relcache init file path, while a thread keeps issuing DDLs so the tserver response cache
   * never serves those preloads.  Connections are offered at a fixed rate and disconnect as soon
   * as they are up, so the number of backends alive at once follows how long the master is taking
   * to serve a prefetch rather than how many this test happened to open.
   *
   * What it asserts is what holds on any machine: every connection completes, none fails, and the
   * cluster still serves afterwards.  Rejected prefetches retrying and completing is the property
   * under test, and it does not depend on the size of the box.  It also fails if concurrency
   * climbs past a ceiling, which is what running out of memory looks like one step before it
   * happens -- except on a sanitizer build, where establishing a connection takes most of ten
   * seconds on its own and the ceiling would measure the instrumentation rather than the leader.
   *
   * Every knob below can be raised from the environment to reproduce the incident at scale on a
   * machine with room for it.
   */
  @Test
  public void testPreloadConnectionStress() throws Exception {
    // The load under test is one catalog prefetch per new backend, so every client connection has
    // to make one. (On master this is the @BypassConnMgr annotation, which does not exist on this
    // branch; testRelcacheInitConnectionStress above opts out the same way.)
    skipYsqlConnMgr(BasePgSQLTest.RELCACHE_INIT_NEEDS_NEW_BACKEND,
                isTestRunningWithConnectionManager());
    // Sized so the default run stays light enough for any build machine; the environment
    // overrides are what turn it into a stress test.
    final boolean isSanitizerBuild = BuildTypeUtil.isASAN() || BuildTypeUtil.isTSAN();
    final int numTables = intFromEnv("YB_STRESS_NUM_TABLES", 10);
    final int columnsPerTable = intFromEnv("YB_STRESS_COLUMNS", 30);
    // A rate every machine can sustain, rather than one derived from the core count. What limits
    // the rate is the cost of creating a backend and preloading a catalog into it, and that turns
    // on how fast the cores are rather than how many there are: 16/s ran away on a 4-core VM
    // while 20/s held steady on a 10-core laptop. Saturating a particular leader needs a rate
    // tuned to that machine, which is what the override is for.
    final int connectRate = intFromEnv("YB_STRESS_CONNECT_RATE", isSanitizerBuild ? 1 : 4);
    // Connections that establish promptly leave about connectRate of them alive at once, so this
    // sits well above that: high enough that a slow machine does not reach it in the ordinary
    // course, low enough that a genuine pile-up does long before memory runs out. At the tens of
    // MB a preloaded backend costs, it also caps what this test can take from the machine.
    // The multiplier is how much slower than a second a connection may take to establish before
    // the run is called a pile-up. A sanitizer build takes most of ten seconds over it on its
    // own, so it gets a far wider allowance -- and does not fail on reaching the ceiling at all,
    // since there it measures the instrumentation rather than the leader falling behind.
    final int maxLiveGuard =
        intFromEnv("YB_STRESS_MAX_LIVE", (isSanitizerBuild ? 30 : 8) * connectRate);
    // Exercises the DDL pacing: each DDL waits for the master to work through the prefetches the
    // previous one caused before bumping the version again. -1, the default, leaves the server's
    // own setting alone, which is what a real deployment gets. 0 turns the wait off, which is how
    // this run is compared against one without the pacing.
    final int ddlWaitMs = intFromEnv("YB_STRESS_DDL_WAIT_MS", -1);
    // One, like the migration script this paces. Issuing DDLs from several sessions at once does
    // not churn the catalog version any faster -- measured at about ten a second either way,
    // because the DDL path serialises them rather than the sessions being the limit -- so the
    // extra sessions would buy load the leader never sees while modelling something no customer
    // does. What reaches the leader is one prefetch per catalog version rather than one per
    // connection, since the tserver response cache serves every connection arriving at a version
    // already fetched.
    final int ddlThreads = intFromEnv("YB_STRESS_DDL_THREADS", 1);
    AtomicInteger ddlCount = new AtomicInteger(0);
    AtomicBoolean overGuard = new AtomicBoolean(false);
    final int durationSeconds =
        intFromEnv("YB_STRESS_DURATION_SEC", isSanitizerBuild ? 60 : 180);

    // Preloading every catalog table on every connection is what makes each backend expensive.
    // The admission bound itself rides on an AutoFlag, which a freshly created cluster promotes.
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_catalog_preload_additional_tables", "true");
    // Overrides this class's log_statement=all, which would otherwise write a line for every
    // statement of every connection and fill the disk before memory became the problem.
    tserverFlags.put("ysql_pg_conf_csv", "log_statement=none");
    // Forces each prefetch into a sequence of paged reads, which is the shape the incident had:
    // 172 pages per prefetch, each its own master RPC. It multiplies the master work a connection
    // causes without changing the rows it ends up holding, so master load rises while backend
    // memory does not -- and it is the only way this test produces continuations at all, since a
    // prefetch that fits in one request never has any.
    int prefetchRowLimit = intFromEnv("YB_STRESS_PREFETCH_ROW_LIMIT", 0);
    if (prefetchRowLimit > 0) {
      tserverFlags.put("ysql_catalog_prefetch_row_limit", Integer.toString(prefetchRowLimit));
    }
    // Batching is by bytes by default, so shrinking this pages the same way production does,
    // rather than through the row limit above, which nothing sets any more. The limit is split
    // across the catalog tables active in a round, so a small value multiplies the rounds.
    int prefetchSizeLimit = intFromEnv("YB_STRESS_PREFETCH_SIZE_LIMIT", 0);
    if (prefetchSizeLimit > 0) {
      tserverFlags.put("ysql_catalog_prefetch_size_limit", Integer.toString(prefetchSizeLimit));
    }
    // Object locking is the first thing to break at this connection count: every backend takes
    // AccessShare on pg_proc, and the pending fastpath requests share one node-wide buffer, which
    // once full makes every further request log a warning. Here that buffer is the compile-time
    // kMaxFastpathRequests of 4096, which is ample for the connection counts this test reaches, so
    // there is nothing to size. (On master it is the object_lock_fastpath_buffer_size gflag, which
    // defaults to 256 and the original of this test raises; the gflag does not exist on this
    // branch.) The rest of the DDL path runs as a release build runs it -- object locking and
    // transactional DDL are tied together by a validator, so turning the first off would take the
    // second with it.
    // YB_STRESS_PREFETCH_LIMIT pins the bound rather than letting the master derive it from the
    // core count, which is useful both for forcing rejections on a machine too small to reach the
    // derived limit by load alone and for turning the bound off. The flag lives only in the
    // master, so it must not be passed to the tservers.
    Map<String, String> masterFlags = new HashMap<>();
    // Caps what the leader believes it has, without touching the tservers. The incident had one
    // master serving two hundred nodes, so the leader was the scarce resource while every backend
    // had a machine to itself; on one box everything shares the same cores and the machine gives
    // out before the master does. Starving only the master restores that asymmetry: prefetches
    // take longer to serve and hold their slots longer, which is what makes concurrency climb.
    int masterCpus = intFromEnv("YB_STRESS_MASTER_CPUS", 0);
    if (masterCpus > 0) {
      masterFlags.put("num_cpus", Integer.toString(masterCpus));
    }
    // -1 leaves the master to derive the limit from the core count. 0 turns the bound off
    // altogether, which is what AdmitRead does before it counts anything, so it stands in for a
    // build without this fix without having to revert and rebuild one.
    int prefetchLimit = intFromEnv("YB_STRESS_PREFETCH_LIMIT", -1);
    if (prefetchLimit >= 0) {
      masterFlags.put("master_max_concurrent_ysql_catalog_prefetches",
                      Integer.toString(prefetchLimit));
    }
    restartClusterWithFlags(masterFlags, tserverFlags);
    if (masterCpus == 1) {
      confineMastersToOneCpu();
    }

    ExecutorService executorService = Executors.newCachedThreadPool();
    AtomicInteger numSuccesses = new AtomicInteger(0);
    AtomicInteger numFailures = new AtomicInteger(0);
    AtomicBoolean stopBumper = new AtomicBoolean(false);
    AtomicBoolean stopMonitor = new AtomicBoolean(false);

    try (Connection connSuperuser = getConnectionBuilder().connect();
         Statement stmtSuperuser = connSuperuser.createStatement()) {

      // A catalog big enough that preloading it costs real memory. Columns matter as much as
      // tables here, because pg_attribute is what grows.
      LOG.info("Creating {} tables of {} columns...", numTables, columnsPerTable);
      StringBuilder columns = new StringBuilder();
      for (int c = 0; c < columnsPerTable; c++) {
        columns.append(String.format(", c%d text", c));
      }
      for (int t = 0; t < numTables; t++) {
        stmtSuperuser.execute(
            String.format("CREATE TABLE stress_%d (k int PRIMARY KEY%s)", t, columns));
        if ((t + 1) % 100 == 0) {
          LOG.info("Created {} of {} tables", t + 1, numTables);
        }
      }

      long rejectionsBefore = getMasterPrefetchMetric("ysql_catalog_prefetch_rejections");
      LOG.info("Starting catalog versions: {}", getCatalogVersions(stmtSuperuser));

      // Keep the catalog version moving, so that the tserver response cache cannot serve these
      // preloads and every connection goes to the master leader for its own copy. Real DDLs
      // rather than a direct call to yb_increment_all_db_catalog_versions_with_inval_messages:
      // the pacing wait runs at the start of a DDL that is going to bump the version, so only a
      // real DDL exercises it, and a migration script running DDLs in a row is the case the
      // pacing exists for. The thread stops on its own as well as on the flag: left running
      // unattended by a test that hangs or throws, it keeps the whole cluster churning catalog
      // versions with nobody watching.
      final long bumperDeadline =
          System.currentTimeMillis() + (durationSeconds + 300) * 1000L;
      LOG.info("Starting {} DDL threads, pacing wait {} ms.", ddlThreads, ddlWaitMs);
      for (int t = 0; t < ddlThreads; t++) {
        // Each thread needs a table of its own, or they would collide on the same name rather
        // than churning the catalog version in parallel.
        final String probeTable = "yb_ddl_pacing_probe_" + t;
        executorService.submit(() -> {
          try (Connection bumperConn = getConnectionBuilder().connect();
               Statement bumperStmt = bumperConn.createStatement()) {
            if (ddlWaitMs >= 0) {
              bumperStmt.execute(
                  "SET yb_ddl_wait_for_master_prefetch_drain_ms = " + ddlWaitMs);
            }
            while (!stopBumper.get() && System.currentTimeMillis() < bumperDeadline) {
              try {
                // A create and a drop of the same table, so the schema does not grow over the run
                // while each statement still bumps the catalog version.
                bumperStmt.execute("CREATE TABLE " + probeTable + " (k int)");
                ddlCount.incrementAndGet();
                bumperStmt.execute("DROP TABLE " + probeTable);
                ddlCount.incrementAndGet();
              } catch (Exception e) {
                if (!stopBumper.get()) {
                  LOG.error("Error running DDL: {}", e.getMessage());
                }
              }
            }
          } catch (Exception connEx) {
            LOG.error("Failed to set up connection for DDL thread: {}", connEx.getMessage());
          }
        });
      }

      // Offer connections at a steady rate rather than in one burst, and let each one go as soon
      // as it has run a query. How many backends are alive at once is then arrival rate times how
      // long a connection takes to establish, and establishing is what a saturated master slows
      // down: the memory the machine has to hold becomes a consequence of master speed rather than
      // of how many connections this test happened to open at once. That is the shape of the
      // incident, where the application kept offering connections at its own rate however slow the
      // server had become.
      LOG.info("Offering {} connections per second for {} seconds across {} tservers, each "
               + "preloading the catalog and then disconnecting...",
               connectRate, durationSeconds, miniCluster.getNumTServers());
      Properties props = new Properties();
      props.setProperty("loginTimeout", "600");
      props.setProperty("socketTimeout", "600");

      AtomicInteger live = new AtomicInteger(0);
      AtomicInteger maxLive = new AtomicInteger(0);
      // Report as the run proceeds. An earlier attempt took the machine down before reaching the
      // summary below, so nothing was known afterwards about whether the bound had engaged.
      executorService.submit(() -> {
        while (!stopMonitor.get()) {
          try {
            Thread.sleep(5000);
            LOG.info("progress: completed={} failed={} live={} maxLive={} ddls={} "
                     + "masterRejections={}",
                     numSuccesses.get(), numFailures.get(), live.get(), maxLive.get(),
                     ddlCount.get(),
                     getMasterPrefetchMetric("ysql_catalog_prefetch_rejections"));
          } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return;
          } catch (Exception e) {
            LOG.warn("Could not read master prefetch metrics: {}", e.getMessage());
          }
        }
      });

      AtomicInteger outstanding = new AtomicInteger(0);
      // Spread across the tservers rather than letting them all land on the first one. The
      // response cache is per tserver, so what reaches the leader is one prefetch per catalog
      // version per tserver: connecting to a single one collapses the whole run onto one cache
      // and removes the fan-in this is meant to reproduce, where every node fetched its own copy.
      AtomicInteger nextTserver = new AtomicInteger(0);
      final int numTservers = miniCluster.getNumTServers();
      long deadline = System.currentTimeMillis() + durationSeconds * 1000L;
      while (System.currentTimeMillis() < deadline && !overGuard.get()) {
        for (int i = 0; i < connectRate; i++) {
          outstanding.incrementAndGet();
          executorService.submit(() -> {
            maxLive.accumulateAndGet(live.incrementAndGet(), Math::max);
            final int tserver = Math.floorMod(nextTserver.getAndIncrement(), numTservers);
            try (Connection conn = getConnectionBuilder().withTServer(tserver).connect(props);
                 Statement stmt = conn.createStatement()) {
              stmt.executeQuery("SELECT 1").close();
              numSuccesses.incrementAndGet();
            } catch (Exception e) {
              numFailures.incrementAndGet();
              LOG.warn("Connection failed: {}", e.getMessage());
            } finally {
              live.decrementAndGet();
              outstanding.decrementAndGet();
            }
          });
        }
        // Backends that pile up faster than they drain are what takes the machine down, and a
        // dead machine costs a reboot and tells us nothing. Stop offering and report instead.
        if (live.get() >= maxLiveGuard) {
          overGuard.set(true);
          LOG.error("Stopped offering at {} backends alive at once, the YB_STRESS_MAX_LIVE={} "
                    + "ceiling: connections are establishing more slowly than they are offered, "
                    + "so backends are piling up faster than they drain.",
                    live.get(), maxLiveGuard);
        }
        Thread.sleep(1000);
      }

      LOG.info("Done offering connections; waiting for the ones still establishing...");
      while (outstanding.get() > 0) {
        Thread.sleep(500);
      }
      stopMonitor.set(true);
      LOG.info("Connections completed: {}, failed: {}, most alive at once: {}, DDLs run: {}",
               numSuccesses.get(), numFailures.get(), maxLive.get(), ddlCount.get());

      stopBumper.set(true);
      executorService.shutdown();
      if (!executorService.awaitTermination(120, TimeUnit.SECONDS)) {
        LOG.warn("Executor service did not terminate gracefully.");
        executorService.shutdownNow();
      }

      LOG.info("Ending catalog versions: {}", getCatalogVersions(stmtSuperuser));
      // Non-zero means the bound engaged and shed load rather than letting every prefetch in.
      LOG.info("Master prefetch rejections during the run: {}",
               getMasterPrefetchMetric("ysql_catalog_prefetch_rejections") - rejectionsBefore);
    } finally {
      // However the run ends. The guard assertion below, or a SQL error anywhere above, would
      // otherwise skip the shutdown and leave the progress thread looping for as long as the JVM
      // lives.
      stopMonitor.set(true);
      stopBumper.set(true);
      executorService.shutdownNow();
    }


    // The point of the run: the cluster is still there afterwards.
    try (Connection check = getConnectionBuilder().connect();
         Statement stmt = check.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT 1");
      assertTrue("cluster stopped serving connections after the stress run", rs.next());
    }

    // Asserted last, so the counters above reach the log either way. Reaching the ceiling is what
    // running out of memory looks like one step before it happens: with the bound and the pacing
    // in place, connections establish quickly enough that concurrency settles far below it, and
    // without them they accumulate until something gives. Failing here rather than letting the
    // machine die keeps the log that explains why.
    if (!isSanitizerBuild) {
      assertFalse(
          String.format("backends alive at once reached %d: they piled up faster than the master "
                        + "could serve their prefetches", maxLiveGuard),
          overGuard.get());
    }
  }
}
