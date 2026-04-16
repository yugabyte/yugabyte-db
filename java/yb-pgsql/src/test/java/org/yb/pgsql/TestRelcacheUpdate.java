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
import java.util.Arrays;
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
    boolean isSanitizerBuild = BuildTypeUtil.isASAN() || BuildTypeUtil.isTSAN();
    // Number of databases and connections per DB.
    final int NUM_DATABASES = 10;
    final int CONNECTIONS_PER_DB = isSanitizerBuild ? 10 : 20;
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
    // Assert the variations between connections are less than 20%.
    double maxVariationPercent = (double)(maxRSS - minRSS) / minRSS;
    LOG.info("maxVariationPercent {}", maxVariationPercent);
    assertTrue("Expected maxVariationPercent less than 20%", maxVariationPercent < 0.20);
  }
}
