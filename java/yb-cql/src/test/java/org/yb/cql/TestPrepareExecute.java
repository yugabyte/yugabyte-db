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
package org.yb.cql;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Vector;
import java.lang.reflect.Field;

import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.DataType;
import com.datastax.driver.core.PreparedId;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.exceptions.NoHostAvailableException;
import com.datastax.driver.core.exceptions.QueryValidationException;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import org.yb.util.BuildTypeUtil;
import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestPrepareExecute extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPrepareExecute.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 600; // Usual time for a multi-threaded test ~40 seconds.
  }             // But it can be much more on Jenkins.

  @Override
  protected Map<String, String> getTServerFlags() {
    // Set up prepare statement cache size. Each SELECT statement below takes about 16kB (two 4kB
    // memory page for the parse tree and two for semantic analysis results). A 64kB cache
    // should be small enough to force prepared statements to be freed and reprepared.
    // Note: add "--v=1" below to see the prepared statement cache usage in trace output.
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("cql_service_max_prepared_statement_size_bytes", "65536");
    return flagMap;
  }

  @Test
  public void testBasicPrepareExecute() throws Exception {
    LOG.info("Begin test");

    // Setup table.
    setupTable("test_prepare", 0 /* num_rows */);

    String insert_stmt =
        "insert into test_prepare (h1, h2, r1, r2, v1, v2) values (1, 'a', 2, 'b', 3, 'c');";
    session.execute(insert_stmt);

    // Prepare and execute statement.
    String select_stmt =
        "select * from test_prepare where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';";
    PreparedStatement stmt = session.prepare(select_stmt);
    ResultSet rs = session.execute(stmt.bind());
    Row row = rs.one();

    // Assert that the row is returned.
    assertNotNull(row);
    assertEquals(1, row.getInt("h1"));
    assertEquals("a", row.getString("h2"));
    assertEquals(2, row.getInt("r1"));
    assertEquals("b", row.getString("r2"));
    assertEquals(3, row.getInt("v1"));
    assertEquals("c", row.getString("v2"));

    LOG.info("End test");
  }

  protected enum MetadataInExecResp { ON, OFF; }
  protected enum UseMetadataCache { ON, OFF; }

  protected void doTestAlterAdd(MetadataInExecResp inResp,
                                UseMetadataCache useCache) throws Exception {
    // Setup table.
    setupTable("test_prepare", 0 /* num_rows */);
    session.execute(
        "INSERT INTO test_prepare (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c');");

    // Prepare and execute statement.
    String selectStmt =
        "SELECT * FROM test_prepare WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b';";
    PreparedStatement prepared = session.prepare(selectStmt);

    Row row = session.execute(prepared.bind()).one();
    assertEquals(6, row.getColumnDefinitions().size());
    assertEquals("Row[1, a, 2, b, 3, c]", row.toString());

    // Second connection.
    try (Session s2 = connectWithTestDefaults().getSession()) {
      s2.execute("USE " + DEFAULT_TEST_KEYSPACE);
      s2.execute("ALTER TABLE test_prepare ADD v3 int");
      s2.execute("INSERT INTO test_prepare (h1, h2, r1, r2, v3) VALUES (1, 'a', 2, 'b', 9);");
    }
    Thread.sleep(3000);

    // The driver uses the incoming schema info from the CQL response with the new column.
    row = session.execute(prepared.bind()).one();
    if (inResp == MetadataInExecResp.ON) {
      assertEquals(7, row.getColumnDefinitions().size());
      assertEquals("Row[1, a, 2, b, 3, c, 9]", row.toString());
    } else {
      assertEquals(6, row.getColumnDefinitions().size());
      assertEquals("Row[1, a, 2, b, 3, c]", row.toString());
    }

    if (useCache == UseMetadataCache.ON) {
      // Run EXECUTE on all TSes to reset internal Table Metadata cache.
      final int numTServers = miniCluster.getTabletServers().size();
      // First TS was used above.
      for (int i = 0; i < numTServers - 1; ++i) {
        row = session.execute(prepared.bind()).one();
      }
    }

    // Run a new "application" = new driver instance = new cluster object & connection.
    try (Session s3 = connectWithTestDefaults().getSession()) {
      s3.execute("USE " + DEFAULT_TEST_KEYSPACE);
      prepared = s3.prepare(selectStmt);
      row = s3.execute(prepared.bind()).one();
      // Ensure the new driver instance knows the new column.
      assertEquals(7, row.getColumnDefinitions().size());
      assertEquals("Row[1, a, 2, b, 3, c, 9]", row.toString());
    }
  }

  @Test
  public void testAlterAdd() throws Exception {
    try {
      // By default: cql_always_return_metadata_in_execute_response=false.
      restartClusterWithFlag("cql_use_metadata_cache_for_schema_version_check", "false");
      doTestAlterAdd(MetadataInExecResp.OFF, UseMetadataCache.OFF);
    } finally {
      destroyMiniCluster(); // Destroy the recreated cluster when done.
    }
  }

  @Test
  public void testAlterAdd_MetadataInExecResp() throws Exception {
    try {
      Map<String, String> tserverFlags = new HashMap<>();
      tserverFlags.put("cql_always_return_metadata_in_execute_response", "true");
      tserverFlags.put("cql_use_metadata_cache_for_schema_version_check", "false");
      restartClusterWithTSFlags(tserverFlags);
      doTestAlterAdd(MetadataInExecResp.ON, UseMetadataCache.OFF);
    } finally {
      destroyMiniCluster(); // Destroy the recreated cluster when done.
    }
  }

  @Test
  public void testAlterAdd_UseMetadataCache() throws Exception {
    // By default: cql_always_return_metadata_in_execute_response=false.
    //             cql_use_metadata_cache_for_schema_version_check=true.
    doTestAlterAdd(MetadataInExecResp.OFF, UseMetadataCache.ON);
  }

  @Test
  public void testAlterAdd_MetadataInExecResp_UseMetadataCache() throws Exception {
    try {
      restartClusterWithFlag("cql_always_return_metadata_in_execute_response", "true");
      // By default: cql_use_metadata_cache_for_schema_version_check=true.
      doTestAlterAdd(MetadataInExecResp.ON, UseMetadataCache.ON);
    } finally {
      destroyMiniCluster(); // Destroy the recreated cluster when done.
    }
  }

  protected abstract class TestThreadBody implements Runnable {
    protected final String statement;
    public final int threadIndex;

    protected Session session = null;
    protected PreparedStatement prepared = null;
    protected Thread thread = null;
    // Thread results:
    public volatile int errors = 0;
    public volatile int internalErrors = 0;
    public volatile int numOldReq = 0; // Number of successfully executed OLD requests.
    public volatile int numNewReq = 0; // Number of successfully executed NEW requests.

    protected TestThreadBody(String stmt, int index) {
      this.statement = stmt;
      this.threadIndex = index;
      this.thread = new Thread(this, "TestAppThread-" + index);

      thread.start();
      LOG.info("Started thread: " + thread.getName());
    }

    protected void connectAndPrepare() {
      try {
        if (session != null) {
          session.close();
        }

        session = connectWithTestDefaults().getSession();
        session.execute("USE " + DEFAULT_TEST_KEYSPACE);
      } catch (Exception e) {
        LOG.error("CONNECT: Exception caught:", e);
        ++internalErrors;
        throw e;
      }

      try {
        prepared = session.prepare(statement);
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        // Undefined Column
        LOG.warn("PREPARE: Ignoring expected exception:", e);
        prepared = null;
      } catch (Exception e) {
        LOG.error("PREPARE: Exception caught:", e);
        ++internalErrors;
        throw e;
      }
    }

    protected abstract void runRound();

    @Override
    public void run() {
      try {
        while (!Thread.interrupted()) {
          if (prepared == null) {
            connectAndPrepare();
          }

          if (prepared != null) {
            runRound();
          }

          try {
            Thread.sleep(100);
          } catch (InterruptedException iex) {
            LOG.warn("Ignoring caught InterruptedException:", iex);
            Thread.currentThread().interrupt();
            break;
          }
        }

        LOG.info("Completed thread: " + Thread.currentThread().getName());
      } catch (Exception e) {
        LOG.error("Internal exception caught:", e);
        ++internalErrors;
        throw e;
      }
    }
  }

  protected class AlterAddThreadBody extends TestThreadBody {
    public AlterAddThreadBody(String stmt, int index) {
      super(stmt, index);
    }

    @Override
    public void runRound() {
      try {
        String rowStr = session.execute(prepared.bind()).one().toString();
        if (rowStr.equals("Row[1, a, 2, b, 3, c]")) {
          ++numOldReq;
        } else if (rowStr.equals("Row[1, a, 2, b, 3, c, NULL]")) {
          ++numNewReq;
        } else {
          ++errors;
        }
      } catch (Exception e) {
        LOG.error("Exception caught:", e);
        ++internalErrors;
        throw e;
      }
    }
  }

  @Test
  public void testMultiThreadedAlterAdd() throws Exception {
    // Setup table.
    setupTable("test_prepare", 0 /* num_rows */);
    session.execute(
        "INSERT INTO test_prepare (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c');");

    // Prepare and execute statement.
    String selectStmt =
        "SELECT * FROM test_prepare WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b';";
    PreparedStatement prepared = session.prepare(selectStmt);

    Row row = session.execute(prepared.bind()).one();
    assertEquals(6, row.getColumnDefinitions().size());
    assertEquals("Row[1, a, 2, b, 3, c]", row.toString());

    int numThreads = BuildTypeUtil.nonTsanVsTsan(10, 4);
    List<AlterAddThreadBody> threads = new ArrayList<AlterAddThreadBody>();
    // Start half of threads before the ALTER TABLE.
    while (threads.size() != numThreads/2) {
      threads.add(new AlterAddThreadBody(selectStmt, threads.size()));
    }

    Thread.sleep(4000);
    LOG.info("Call: ALTER TABLE");
    session.execute("ALTER TABLE test_prepare ADD v3 int");
    LOG.info("Done: ALTER TABLE");
    Thread.sleep(3000);

    // Start second half of threads after the ALTER TABLE.
    while (threads.size() != numThreads) {
      threads.add(new AlterAddThreadBody(selectStmt, threads.size()));
    }
    // Wait for first successful new INSERT.
    Thread.sleep(4000);
    AlterAddThreadBody lastBody = threads.get(threads.size() - 1);
    final long startTimeMs = System.currentTimeMillis();
    while (lastBody.numNewReq == 0 && (System.currentTimeMillis() - startTimeMs) < 60000) {
      Thread.sleep(1000);
    }

    assertGreaterThan(lastBody.numNewReq, 0);
    // Finish all threads.
    for (AlterAddThreadBody body : threads) {
      body.thread.interrupt();
    }
    for (AlterAddThreadBody body : threads) {
      body.thread.join();
      LOG.info("Finished thread " + body.thread.getName() +
          ": internal-errors=" + body.internalErrors + " errors=" + body.errors +
          " 6-col-results=" + body.numOldReq +
          " 7-col-results=" + body.numNewReq);
    }
    // Check results.
    for (AlterAddThreadBody body : threads) {
      assertEquals(0, body.internalErrors);
      assertEquals(0, body.errors);

      if (body.threadIndex < numThreads/2) {
        // First set of threads should have old results.
        assertGreaterThan(body.numOldReq, 0);
        assertEquals(0, body.numNewReq);
      } else {
        // Second set of threads should have new results because it's started after ALTER TABLE.
        assertEquals(0, body.numOldReq);
        assertGreaterThan(body.numNewReq, 0);
      }
    }
  }

  @Test
  public void testAlterDropAdd() throws Exception {
    // Setup table.
    session.execute("CREATE TABLE test_prepare (h TEXT, r INT, v SET<INT>, PRIMARY KEY(h, r))");

    // Prepare and execute statement.
    String insertStmt = "INSERT INTO test_prepare (h, r, v) VALUES (?, ?, ?)";
    PreparedStatement prepared = session.prepare(insertStmt);
    assertEquals(3, prepared.getVariables().size());
    assertEquals("set<int>", prepared.getVariables().getType(2).toString());

    session.execute(prepared.bind(
        new String("a"), new Integer(1), new HashSet<Integer>(Arrays.asList(1))));
    Row row = session.execute("SELECT * FROM test_prepare").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, [1]]", row.toString());

    // Second connection.
    try (Session s2 = connectWithTestDefaults().getSession()) {
      s2.execute("USE " + DEFAULT_TEST_KEYSPACE);
      // Replace SET type by MAP type.
      s2.execute("ALTER TABLE test_prepare DROP v");
      s2.execute("ALTER TABLE test_prepare ADD v MAP<INT, INT>");
    }

    // Try to re-prepare, but it does not update the cached statement.
    prepared = session.prepare(insertStmt);
    assertEquals(3, prepared.getVariables().size());
    assertEquals("set<int>", prepared.getVariables().getType(2).toString());

    BoundStatement bound = prepared.bind(
        new String("a"), new Integer(1), new HashSet<Integer>(Arrays.asList(2)));
    try {
      session.execute(bound);
      fail("Execution did not fail as expected");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException ex) {
      LOG.info("Ignoring expected InvalidQueryException:", ex);
      assertTrue(ex.getMessage().contains("Invalid Arguments"));
    }

    Thread.sleep(3000);
    row = session.execute("SELECT * FROM test_prepare WHERE h='a' AND r=1").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, NULL]", row.toString());

    // Run a new "application" = new driver instance = new cluster object & connection.
    // The cached schema can be updated after several seconds.
    final long startTimeMs = System.currentTimeMillis();
    boolean schemaIsUpdated = false;
    while (!schemaIsUpdated && (System.currentTimeMillis() - startTimeMs) < 60000) {
      try (Session s3 = connectWithTestDefaults().getSession()) {
        s3.execute("USE " + DEFAULT_TEST_KEYSPACE);
        prepared = s3.prepare(insertStmt);
        assertEquals(3, prepared.getVariables().size());
        final String typeName = prepared.getVariables().getType(2).toString();

        if (typeName.equals("map<int, int>")) {
          schemaIsUpdated = true;
          // Ensure the new driver instance knows the new column.
          s3.execute(prepared.bind(new String("a"), new Integer(1),
              new HashMap<Integer, Integer>() {{ put(9, 9); }}));
          row = session.execute("SELECT * FROM test_prepare WHERE h='a' AND r=1").one();
          assertEquals(3, row.getColumnDefinitions().size());
          assertEquals("Row[a, 1, {9=9}]", row.toString());
        } else {
          LOG.warn("Got type: '" + typeName + "' Sleep...");
          Thread.sleep(500);
        }
      }
    }

    assertTrue(schemaIsUpdated);
  }

  @Test
  public void testAlterDropAddSameSizeType() throws Exception {
    // Setup table.
    session.execute("CREATE TABLE test_prepare (h TEXT, r INT, v BIGINT, PRIMARY KEY(h, r))");

    // Prepare and execute statement.
    String insertStmt = "INSERT INTO test_prepare (h, r, v) VALUES (?, ?, ?)";
    PreparedStatement prepared = session.prepare(insertStmt);
    assertEquals(3, prepared.getVariables().size());
    assertEquals("bigint", prepared.getVariables().getType(2).toString());

    session.execute(prepared.bind(new String("a"), new Integer(1), new Long(100)));
    Row row = session.execute("SELECT * FROM test_prepare").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, 100]", row.toString());

    // Second connection.
    try (Session s2 = connectWithTestDefaults().getSession()) {
      s2.execute("USE " + DEFAULT_TEST_KEYSPACE);
      // Replace the Integer 8-byte type by the Floating point 8-byte type.
      s2.execute("ALTER TABLE test_prepare DROP v");
      s2.execute("ALTER TABLE test_prepare ADD v DOUBLE PRECISION");
    }

    row = session.execute("SELECT * FROM test_prepare").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, NULL]", row.toString());

    // Saving BIGINT value 888 into DOUBLE column.
    session.execute(prepared.bind(new String("a"), new Integer(1), new Long(888)));

    row = session.execute("SELECT * FROM test_prepare WHERE h='a' AND r=1").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, 4.387E-321]", row.toString());

    // Try to re-prepare, but it does not update the cached statement.
    prepared = session.prepare(insertStmt);
    assertEquals(3, prepared.getVariables().size());
    assertEquals("bigint", prepared.getVariables().getType(2).toString());

    // Saving BIGINT value 999 into DOUBLE column.
    session.execute(prepared.bind(new String("a"), new Integer(1), new Long(999)));

    row = session.execute("SELECT * FROM test_prepare WHERE h='a' AND r=1").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, 4.936E-321]", row.toString());

    // Run a new "application" = new driver instance = new cluster object & connection.
    // The cached schema can be updated after several seconds.
    final long startTimeMs = System.currentTimeMillis();
    boolean schemaIsUpdated = false;
    while (!schemaIsUpdated && (System.currentTimeMillis() - startTimeMs) < 60000) {
      try (Session s3 = connectWithTestDefaults().getSession()) {
        s3.execute("USE " + DEFAULT_TEST_KEYSPACE);
        prepared = s3.prepare(insertStmt);
        assertEquals(3, prepared.getVariables().size());
        final String typeName = prepared.getVariables().getType(2).toString();

        if (typeName.equals("double")) {
          schemaIsUpdated = true;
          // Ensure the new driver instance knows the new column.
          s3.execute(prepared.bind(new String("a"), new Integer(1), new Double(3.14)));
          row = session.execute("SELECT * FROM test_prepare WHERE h='a' AND r=1").one();
          assertEquals(3, row.getColumnDefinitions().size());
          assertEquals("Row[a, 1, 3.14]", row.toString());
        } else {
          LOG.warn("Got type: '" + typeName + "' Sleep...");
          Thread.sleep(500);
        }
      }
    }

    assertTrue(schemaIsUpdated);
  }

  protected class AlterDropAddThreadBody extends TestThreadBody {
    public AlterDropAddThreadBody(String stmt, int index) {
      super(stmt, index);
    }

    @Override
    public void runRound() {
      boolean insertSetFailed = false, insertMapFailed = false;
      // Try to INSERT set<int>.
      try {
        session.execute(prepared.bind(
            new String("a"), new Integer(1), new HashSet<Integer>(Arrays.asList(1))));
        Row row = session.execute("SELECT * FROM test_prepare WHERE h='a' AND r=1").one();
        if (3 == row.getColumnDefinitions().size() && row.toString().equals("Row[a, 1, [1]]")) {
          ++numOldReq;
        } else {
          insertSetFailed = true;
        }
      } catch (com.datastax.driver.core.exceptions.CodecNotFoundException e) {
        // Codec not found for requested operation: [map<int, int> <-> java.util.HashSet]
        LOG.warn("INSERT set: Ignoring expected CodecNotFoundException:", e);
        insertSetFailed = true;
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        // Execution Error. Column id 12 not found
        LOG.warn("INSERT set: Ignoring expected InvalidQueryException:", e);
        insertSetFailed = true;
      } catch (com.datastax.driver.core.exceptions.NoHostAvailableException e) {
        // Error preparing query, got ERROR INVALID: Undefined Column
        LOG.warn("INSERT set: Ignoring expected NoHostAvailableException:", e);
        insertSetFailed = true;
      } catch (Exception e) {
        LOG.error("INSERT set: Exception caught:", e);
        throw e;
      }

      // Try to INSERT map<int, int>.
      try {
        session.execute(prepared.bind(new String("a"), new Integer(1),
            new HashMap<Integer, Integer>() {{ put(9, 9); }}));
        Row row = session.execute("SELECT * FROM test_prepare WHERE h='a' AND r=1").one();
        if (3 == row.getColumnDefinitions().size() && row.toString().equals("Row[a, 1, {9=9}]")) {
          ++numNewReq;
        } else {
          insertMapFailed = true;
        }
      } catch (com.datastax.driver.core.exceptions.CodecNotFoundException e) {
        // Codec not found for requested operation: [set<int> <-> java.util.HashMap]
        LOG.warn("INSERT map: Ignoring expected CodecNotFoundException:", e);
        insertMapFailed = true;
      } catch (Exception e) {
        LOG.error("INSERT map: Exception caught:", e);
        throw e;
      }

      // If both INSERT operations failed.
      if (insertSetFailed && insertMapFailed) {
        ++errors;
        prepared = null; // Restart the connection and rerun PREPARE.
      }
    }
  }

  @Test
  public void testMultiThreadedAlterDropAdd() throws Exception {
    // Setup table.
    session.execute("CREATE TABLE test_prepare (h TEXT, r INT, v SET<INT>, PRIMARY KEY(h, r))");

    // Prepare and execute statement.
    String insertStmt = "INSERT INTO test_prepare (h, r, v) VALUES (?, ?, ?)";
    PreparedStatement prepared = session.prepare(insertStmt);
    assertEquals(3, prepared.getVariables().size());
    assertEquals("set<int>", prepared.getVariables().getType(2).toString());

    session.execute(prepared.bind(
        new String("a"), new Integer(1), new HashSet<Integer>(Arrays.asList(1))));
    Row row = session.execute("SELECT * FROM test_prepare").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, [1]]", row.toString());

    int numThreads = BuildTypeUtil.nonTsanVsTsan(10, 4);
    List<AlterDropAddThreadBody> threads = new ArrayList<AlterDropAddThreadBody>();
    // Start half of threads before the ALTER TABLE.
    while (threads.size() != numThreads/2) {
      threads.add(new AlterDropAddThreadBody(insertStmt, threads.size()));
    }

    Thread.sleep(3000);
    for (AlterDropAddThreadBody body : threads) {
      // Check results.
      LOG.info("Thread " + body.thread.getName() +
          ": internal-errors=" + body.internalErrors + " errors=" + body.errors +
          " set-insert-results=" + body.numOldReq +
          " map-insert-results=" + body.numNewReq);

      if (body.threadIndex < numThreads/2) {
        // First set of threads should have old results.
        assertEquals(0, body.internalErrors);
        assertEquals(0, body.errors);
        assertGreaterThan(body.numOldReq, 0);
        assertEquals(0, body.numNewReq);
      }
    }

    Thread.sleep(1000);
    LOG.info("Call: ALTER TABLE");
    session.execute("ALTER TABLE test_prepare DROP v");
    session.execute("ALTER TABLE test_prepare ADD v MAP<INT, INT>");
    LOG.info("Done: ALTER TABLE");
    Thread.sleep(3000);

    // Start second half of threads after the ALTER TABLE.
    while (threads.size() != numThreads) {
      threads.add(new AlterDropAddThreadBody(insertStmt, threads.size()));
    }
    // Wait for first successful new INSERT.
    Thread.sleep(4000);
    AlterDropAddThreadBody lastBody = threads.get(threads.size() - 1);
    final long startTimeMs = System.currentTimeMillis();
    while (lastBody.numNewReq == 0 && (System.currentTimeMillis() - startTimeMs) < 60000) {
      Thread.sleep(1000);
    }

    assertGreaterThan(lastBody.numNewReq, 0);
    // Finish all threads.
    for (AlterDropAddThreadBody body : threads) {
      body.thread.interrupt();
    }
    for (AlterDropAddThreadBody body : threads) {
      body.thread.join();
      LOG.info("Finished thread " + body.thread.getName() +
          ": internal-errors=" + body.internalErrors + " errors=" + body.errors +
          " set-insert-results=" + body.numOldReq +
          " map-insert-results=" + body.numNewReq);
    }
    // Check results.
    for (AlterDropAddThreadBody body : threads) {
      assertEquals(0, body.internalErrors);

      if (body.threadIndex < numThreads/2) {
        // After ALTER - 'INSERT set' cannot work because the table was changed,
        // but 'INSERT map' cannot work too because PREPARE was not called again.
        assertGreaterThan(body.errors, 0);
        // First set of threads should have old results.
        assertGreaterThan(body.numOldReq, 0);
      } else {
        // Second set of threads should not have old results.
        assertEquals(0, body.errors);
        assertEquals(0, body.numOldReq);
        assertGreaterThan(body.numNewReq, 0);
      }
    }
  }

  @Test
  public void testRecreateTable() throws Exception {
    // Setup table.
    session.execute("USE " + DEFAULT_TEST_KEYSPACE);
    session.execute("CREATE TABLE test_prepare (h TEXT, r INT, v SET<INT>, PRIMARY KEY(h, r))");

    // Prepare and execute statement.
    String insertStmt = "INSERT INTO test_prepare (h, r, v) VALUES (?, ?, ?)";
    PreparedStatement prepared = session.prepare(insertStmt);
    assertEquals(3, prepared.getVariables().size());
    assertEquals("set<int>", prepared.getVariables().getType(2).toString());

    session.execute(prepared.bind(
        new String("a"), new Integer(1), new HashSet<Integer>(Arrays.asList(1))));
    Row row = session.execute("SELECT * FROM test_prepare").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, [1]]", row.toString());

    // Second connection.
    try (Session s2 = connectWithTestDefaults().getSession()) {
      s2.execute("USE " + DEFAULT_TEST_KEYSPACE);
      s2.execute("DROP TABLE test_prepare");
      s2.execute("CREATE TABLE test_prepare (h TEXT, r INT, v MAP<INT, INT>, PRIMARY KEY(h, r))");
      s2.execute("INSERT INTO test_prepare (h, r, v) VALUES ('a', 1, {4:5})");
    }

    // Try to insert into the deleted table.
    BoundStatement bound = prepared.bind(
        new String("a"), new Integer(1), new HashSet<Integer>(Arrays.asList(2)));
    try {
      session.execute(bound);
      fail("Execution did not fail as expected");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException ex) {
      LOG.info("Expected exception", ex);
      assertTrue(ex.getMessage().contains("Invalid Arguments"));
    }

    // Try to re-prepare, but it does not update the cached statement.
    prepared = session.prepare(insertStmt);
    assertEquals(3, prepared.getVariables().size());
    assertEquals("set<int>", prepared.getVariables().getType(2).toString());

    bound = prepared.bind(new String("a"), new Integer(1), new HashSet<Integer>(Arrays.asList(2)));
    try {
      session.execute(bound);
      fail("Execution did not fail as expected");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException ex) {
      LOG.info("Expected exception", ex);
      assertTrue(ex.getMessage().contains("Invalid Arguments"));
    }

    row = session.execute("SELECT * FROM test_prepare WHERE h='a' and r=1").one();
    assertEquals(3, row.getColumnDefinitions().size());
    assertEquals("Row[a, 1, {4=5}]", row.toString());

    // Run a new "application" = new driver instance = new cluster object & connection.
    try (Session s3 = connectWithTestDefaults().getSession()) {
      s3.execute("USE " + DEFAULT_TEST_KEYSPACE);
      prepared = s3.prepare(insertStmt);
      assertEquals(3, prepared.getVariables().size());
      assertEquals("map<int, int>", prepared.getVariables().getType(2).toString());

      // Ensure the new driver instance knows the new column.
      s3.execute(prepared.bind(new String("a"), new Integer(1),
          new HashMap<Integer, Integer>() {{ put(9, 9); }}));
      row = session.execute("SELECT * FROM test_prepare WHERE h='a' and r=1").one();
      assertEquals(3, row.getColumnDefinitions().size());
      assertEquals("Row[a, 1, {9=9}]", row.toString());
    }
  }

  @Test
  public void testMultiplePrepareExecute() throws Exception {
    LOG.info("Begin test");

    // Setup table.
    setupTable("test_prepare", 0 /* num_rows */);

    // Insert 10 rows.
    for (int i = 0; i < 10; i++) {
      String insert_stmt = String.format(
          "insert into test_prepare (h1, h2, r1, r2, v1, v2) values (%d, 'a', 2, 'b', %d, 'c');",
          i, i + 1);
      session.execute(insert_stmt);
    }

    // Prepare 10 statements, each for each of the 10 rows.
    Vector<PreparedStatement> stmts = new Vector<PreparedStatement>();
    for (int i = 0; i < 10; i++) {
      String select_stmt = String.format(
          "select * from test_prepare where h1 = %d and h2 = 'a' and r1 = 2 and r2 = 'b';", i);
      stmts.add(i, session.prepare(select_stmt));
    }

    // Execute the 10 prepared statements round-robin. Loop for 3 times.
    for (int j = 0; j < 3; j++) {
      for (int i = 0; i < 10; i++) {
        ResultSet rs = session.execute(stmts.get(i).bind());
        Row row = rs.one();

        // Assert that the expected row is returned.
        assertNotNull(row);
        assertEquals(i, row.getInt("h1"));
        assertEquals("a", row.getString("h2"));
        assertEquals(2, row.getInt("r1"));
        assertEquals("b", row.getString("r2"));
        assertEquals(i + 1, row.getInt("v1"));
        assertEquals("c", row.getString("v2"));
      }
    }

    LOG.info("End test");
  }

  @Test
  public void testExecuteAfterTableDrop() throws Exception {
    LOG.info("Begin test");

    // Setup table.
    setupTable("test_prepare", 1 /* num_rows */);

    // Prepare statement.
    PreparedStatement stmt = session.prepare("select * from test_prepare;");

    // Drop the table.
    session.execute("drop table test_prepare;");

    // Execute the prepared statement. Expect failure because of the table drop.
    try {
      ResultSet rs = session.execute(stmt.bind());
      fail("Prepared statement did not fail to execute after table is dropped");
    } catch (NoHostAvailableException e) {
      LOG.info("Expected exception caught: " + e.getMessage());
    }

    LOG.info("End test");
  }

  @Test
  public void testExecuteWithUnknownSystemTable() throws Exception {
    LOG.info("Begin test");

    // Prepare statement.
    PreparedStatement stmt = session.prepare("select * from system.unknown_table;");

    // Execute the prepared statement. Expect empty row set.
    ResultSet rs = session.execute(stmt.bind());
    assertNull(rs.one());

    LOG.info("End test");
  }

  @Test
  public void testExecuteWithUnknownSystemTableAndUnknownField() throws Exception {
    LOG.info("Begin test");

    // Prepare statement.
    PreparedStatement stmt =
      session.prepare("select * from system.unknown_table where unknown_field = ?;");

    // Execute the prepared statement. Expect empty row set.
    ResultSet rs = session.execute(stmt.bind());
    assertNull(rs.one());

    LOG.info("End test");
  }

  private void testPreparedDDL(String stmt) {
    session.execute(session.prepare(stmt).bind());
  }

  private void testInvalidDDL(String stmt) {
    try {
      session.execute(session.prepare(stmt).bind());
      fail("Prepared statement did not fail to execute");
    } catch (QueryValidationException e) {
      LOG.info("Expected exception caught: " + e.getMessage());
    }
  }

  private void testInvalidPrepare(String stmt) {
    try {
      PreparedStatement pstmt = session.prepare(stmt);
      fail("Prepared statement did not fail to prepare");
    } catch (QueryValidationException e) {
      LOG.info("Expected exception caught: " + e.getMessage());
    }
  }

  @Test
  public void testDDL() throws Exception {
    LOG.info("Begin test");

    // Test execute prepared CREATE/DROP KEYSPACE and TABLE.
    testPreparedDDL("CREATE KEYSPACE k;");
    assertQuery("SELECT keyspace_name FROM system_schema.keyspaces " +
                "WHERE keyspace_name = 'k';",
                "Row[k]");

    testPreparedDDL("CREATE TABLE k.t (k int PRIMARY KEY);");
    assertQuery("SELECT keyspace_name, table_name FROM system_schema.tables "+
                "WHERE keyspace_name = 'k' AND table_name = 't';",
                "Row[k, t]");

    testPreparedDDL("DROP TABLE k.t;");
    assertQuery("SELECT keyspace_name, table_name FROM system_schema.tables "+
                "WHERE keyspace_name = 'k' AND table_name = 't';",
                "");

    testPreparedDDL("DROP KEYSPACE k;");
    assertQuery("SELECT keyspace_name FROM system_schema.keyspaces " +
                "WHERE keyspace_name = 'k';",
                "");

    // Test USE keyspace.
    testPreparedDDL("CREATE KEYSPACE k;");
    testPreparedDDL("USE k;");
    testPreparedDDL("CREATE TABLE t (k int PRIMARY KEY);");
    assertQuery("SELECT keyspace_name, table_name FROM system_schema.tables "+
                "WHERE keyspace_name = 'k' AND table_name = 't';",
                "Row[k, t]");

    // Test invalid DDL: invalid syntax and non-existent keyspace.
    testInvalidDDL("CREATE TABLE k.t2;");
    testInvalidDDL("CREATE TABLE k2.t (k int PRIMARY KEY);");

    LOG.info("End test");
  }

  // Assert metadata returned from a prepared statement.
  private void assertHashKeysMetadata(String stmt, DataType hashKeys[]) throws Exception {
    PreparedStatement pstmt = session.prepare(stmt);
    PreparedId id = pstmt.getPreparedId();

    // TODO: routingKeyIndexes in Cassandra's PreparedId class is not exposed (see JAVA-195).
    // Make it accessible via an API in our fork.

    // Assert routingKeyIndexes have the same number of entries as the hash columns.
    Field f = id.getClass().getDeclaredField("routingKeyIndexes");
    f.setAccessible(true);
    int hashIndices[] = (int[])f.get(id);
    if (hashKeys == null) {
      assertNull(hashIndices);
      return;
    }
    assertEquals(hashKeys.length, hashIndices.length);

    // Assert routingKeyIndexes's order and datatypes in metadata are the same as in the table hash
    // key definition.
    ColumnDefinitions variables = pstmt.getVariables();
    assertTrue(hashKeys.length <= variables.size());
    for (int i = 0; i < hashKeys.length; i++) {
      assertEquals(hashKeys[i], variables.getType(hashIndices[i]));
    }
  }

  @Test
  public void testHashKeysMetadata() throws Exception {

    LOG.info("Create test table.");
    session.execute("CREATE TABLE test_pk_indices " +
                    "(h1 int, h2 text, h3 timestamp, r int, v int, " +
                    "PRIMARY KEY ((h1, h2, h3), r));");

    // Expected hash key columns in the cardinal order.
    DataType hashKeys[] = new DataType[]{DataType.cint(), DataType.text(), DataType.timestamp()};

    LOG.info("Test prepared statements.");

    // Test bind markers "?" in different query and DMLs and in different hash key column orders.
    // The hash key metadata returned should be the same regardless.
    assertHashKeysMetadata("SELECT * FROM test_pk_indices WHERE h3 = ? AND h1 = ? AND h2 = ?;",
                           hashKeys);
    assertHashKeysMetadata("SELECT * FROM test_pk_indices WHERE h2 = ? AND h3 = ? AND h1 = ? " +
                           "AND r < ?;", hashKeys);
    assertHashKeysMetadata("INSERT INTO test_pk_indices (h2, h1, h3, r) VALUES (?, ?, ?, ?);",
                           hashKeys);
    assertHashKeysMetadata("UPDATE test_pk_indices SET v = ? " +
                           "WHERE h1 = ? AND h3 = ? AND r = ? AND h2 = ?;", hashKeys);
    assertHashKeysMetadata("DELETE FROM test_pk_indices " +
                           "WHERE h2 = ? AND h1 = ? AND h3 = ? AND r = ?;", hashKeys);

    // Test bind markers ":x" in different query and DMLs and in different hash key column orders.
    // The hash key metadata returned should be the same regardless still.
    assertHashKeysMetadata("SELECT * FROM test_pk_indices " +
                           "WHERE h3 = :h3 AND h1 = :h1 AND h2 = :h2;", hashKeys);
    assertHashKeysMetadata("SELECT * FROM test_pk_indices " +
                           "WHERE h2 = :h2 AND h3 = :h3 AND h1 = :h1 " +
                           "AND r < :r1;", hashKeys);
    assertHashKeysMetadata("INSERT INTO test_pk_indices (h1, h2, h3, r) " +
                           "VALUES (:h1, :h2, :h3, :r);", hashKeys);
    assertHashKeysMetadata("UPDATE test_pk_indices SET v = :v " +
                           "WHERE h1 = :h1 AND h3 = :h3 AND h2 = :h2 AND r = :r;", hashKeys);
    assertHashKeysMetadata("DELETE FROM test_pk_indices " +
                           "WHERE h2 = :h2 AND h1 = :h1 AND h3 = :h3 AND r = :r;", hashKeys);

    // Test SELECT with partial hash column list. In this case, it becomes a full-table scan.
    // Verify no hash key metadata is returned.
    assertHashKeysMetadata("SELECT * FROM test_pk_indices WHERE h1 = ?;", null);

    // Test with hash column list partially bound. In this case, verify an empty hash key list is
    // returned.
    assertHashKeysMetadata("SELECT * FROM test_pk_indices WHERE h1 = ? and h2 = ? and h3 = 1;",
                           null);
    assertHashKeysMetadata("INSERT INTO test_pk_indices (h1, h2, h3, r) VALUES (1, ?, ?, ?);",
                           null);

    // Test DML with incomplete hash key columns. Verify that the statements fail to prepare at all.
    testInvalidPrepare("INSERT INTO test_pk_indices (h1, h2, r) VALUES (?, ?, ?);");
    testInvalidPrepare("UPDATE test_pk_indices SET v = ? WHERE h1 = ?;");
    testInvalidPrepare("DELETE FROM test_pk_indices WHERE h2 = ? AND h1 = ?;");
  }

  @Test
  public void testDDLKeyspaceResolution() throws Exception {
    // Create 2 test keyspaces.
    session.execute("CREATE KEYSPACE test_k1;");
    session.execute("CREATE KEYSPACE test_k2;");

    // Prepare create DDLs in k1.
    session.execute("USE test_k1;");
    PreparedStatement createTable = session.prepare("CREATE TABLE test_table (h INT PRIMARY KEY);");
    PreparedStatement createType = session.prepare("CREATE TYPE test_type (n INT);");

    // Execute prepared DDLs in k2.
    session.execute("USE test_k2;");
    session.execute(createTable.bind());
    session.execute(createType.bind());

    // Verify that the objects are not created in k2.
    runInvalidStmt("DROP TABLE test_k2.test_table;");
    runInvalidStmt("DROP TYPE test_k2.test_type;");

    // Prepare alter/drop DDLs in k1.
    session.execute("USE test_k1;");
    PreparedStatement alterTable = session.prepare("ALTER TABLE test_table ADD c INT;");
    PreparedStatement dropTable = session.prepare("DROP TABLE test_table;");
    PreparedStatement dropType = session.prepare("DROP TYPE test_type;");

    // Execute prepared DDLs in k2.
    session.execute("USE test_k2;");
    session.execute(alterTable.bind());
    session.execute(dropTable.bind());
    session.execute(dropType.bind());
  }
}
