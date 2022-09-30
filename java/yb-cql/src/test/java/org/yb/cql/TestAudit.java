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

import static org.yb.AssertionWrappers.*;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.function.Function;
import java.util.stream.Collectors;

import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.time.StopWatch;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.BuildTypeUtil;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.SimpleStatement;
import com.google.common.net.HostAndPort;

@RunWith(value = YBTestRunner.class)
public class TestAudit extends BaseCQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestAudit.class);

  private final CurrentAuditRecords auditRecords = new CurrentAuditRecords();

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("use_cassandra_authentication", "true");
    return flags;
  }

  @Override
  public Cluster.Builder getDefaultClusterBuilder() {
    return super.getDefaultClusterBuilder().withCredentials("cassandra", "cassandra");
  }

  @Override
  protected boolean shouldRestartMiniClusterBetweenTests() {
    // We need to restart the cluster in order to reset audit flags to their defaults.
    return true;
  }

  @Before
  public void createRolesAndInitLogListeners() throws Exception {
    for (Entry<HostAndPort, MiniYBDaemon> entry : miniCluster.getTabletServers().entrySet()) {
      int port = entry.getKey().getPort();
      MiniYBDaemon tserver = entry.getValue();
      tserver.getLogPrinter().addErrorListener(new AuditLogListener(port, auditRecords));
    }

    session.execute("CREATE ROLE user1 WITH login = true AND password = '123'");

    AuditConfig config = new AuditConfig();
    config.enabled = true;
    applyAuditConfig(config);
    auditRecords.discard();
  }

  @Test
  public void auth() throws Exception {
    try (Cluster cluster = getCluster("user1", "123");
         Session session = cluster.connect()) {
      List<AuditLogEntry> records = auditRecords.popAll();
      List<AuditLogEntry> authRecords = records.stream()
          .filter(e -> e.category.equals("AUTH"))
          .collect(Collectors.toList());
      assertEquals(4, authRecords.size()); // 3 nodes + 1 control connection
      for (AuditLogEntry s : authRecords) {
        assertEquals("LOGIN_SUCCESS", s.type);
      }
    }

    try (Cluster cluster = getCluster("user1", "321");
         Session session = cluster.connect()) {
      fail("These credentials should be invalid!");
    } catch (com.datastax.driver.core.exceptions.AuthenticationException ex) {
      assertAudit(
          new AuditLogEntry('E', "null", "LOGIN_ERROR", "AUTH",
              null /* batchId */, null /* keyspace */, null /* scope */,
              "LOGIN FAILURE; Provided username 'user1' and/or password are incorrect"));
    } catch (Exception ex) {
      throw ex;
    }

    // Plaintext password should be replaced by <REDACTED>.
    {
      assertAudit(
          "CREATE ROLE user2 WITH login = true AND pAsSWorD=  'hide me!'",
          (cql) -> Arrays.asList(
              new AuditLogEntry('E', "cassandra", "CREATE_ROLE", "DCL",
                  null /* batchId */, null /* keyspace */, null /* scope */,
                  "CREATE ROLE user2 WITH login = true AND pAsSWorD=  <REDACTED>")));

      assertAudit(
          "ALTER ROLE user2 WITH PaSswORd   ='hide me too!'",
          (cql) -> Arrays.asList(
              new AuditLogEntry('E', "cassandra", "ALTER_ROLE", "DCL",
                  null /* batchId */, null /* keyspace */, null /* scope */,
                  "ALTER ROLE user2 WITH PaSswORd   =<REDACTED>")));
    }
  }

  /** Issuing DML batch as YCQL plaintext: {@code START TXN; DML1; DML2; COMMIT} */
  @Test
  public void batchPlaintext() throws Exception {
    session.execute("CREATE TABLE batch_t (id int PRIMARY KEY)"
        + " WITH transactions = {'enabled': 'true'}");
    auditRecords.discard();

    String startTxnCql = "START TRANSACTION";
    String insertCql = "INSERT INTO batch_t (id) VALUES (1)";
    String commitCql = "COMMIT";

    // Execute as one-liner.
    assertAudit(
        StringUtils.joinWith("; ", startTxnCql, insertCql, insertCql, commitCql),
        (cql) -> Arrays.asList(
            new AuditLogEntry('E', "cassandra", "BATCH", "DML",
                null /* batchId */, null /* keyspace */, null /* scope */,
                cql)));

    // Execute line by line.
    // Batch request ID is not present in this case.
    // (For Cassandra, using BEGIN BATCH in this way is grammatically incorrect)
    {
      session.execute(startTxnCql);
      session.execute(insertCql);
      session.execute(insertCql);
      session.execute(commitCql);
      assertAudit(
          new AuditLogEntry('E', "cassandra", "BATCH", "DML",
              null /* batchId */, null /* keyspace */, null /* scope */,
              startTxnCql),
          new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
              null /* batchId */, DEFAULT_TEST_KEYSPACE, "batch_t",
              insertCql),
          new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
              null /* batchId */, DEFAULT_TEST_KEYSPACE, "batch_t",
              insertCql),
          new AuditLogEntry('E', "cassandra", "BATCH", "DML",
              null /* batchId */, null /* keyspace */, null /* scope */,
              commitCql));
    }
  }

  /** Issuing DML batch as driver-level {@code BatchStatement} */
  @Test
  public void batchDriverStatement() throws Exception {
    session.execute("CREATE TABLE batch_t (id int PRIMARY KEY)"
        + " WITH transactions = {'enabled': 'true'}");
    auditRecords.discard();

    String insertCql = "INSERT INTO batch_t (id) VALUES (1)";
    String insertPrepCql = "INSERT INTO batch_t (id) VALUES (?)";

    PreparedStatement preparedInsert = session.prepare(insertPrepCql);
    BatchStatement batch = new BatchStatement()
        .add(new SimpleStatement(insertCql))
        .add(preparedInsert.bind(2))
        .add(preparedInsert.bind(3));

    session.execute(batch);

    // Three PREPARE calls (one per tserver), three UPDATEs

    List<AuditLogEntry> records = auditRecords.popAll();
    String batchId = getBatchId(records);

    AuditLogEntry prepareEntry = new AuditLogEntry('E', "cassandra", "PREPARE_STATEMENT", "PREPARE",
        null /* batchId */, DEFAULT_TEST_KEYSPACE, "batch_t",
        insertPrepCql);
    assertAuditRecords(
        Arrays.asList(
            prepareEntry, prepareEntry, prepareEntry,
            new AuditLogEntry('E', "cassandra", "BATCH", "DML",
                batchId, null /* keyspace */, null /* scope */,
                "BatchId:[" + batchId + "] - BATCH of [3] statements"),
            new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                batchId, DEFAULT_TEST_KEYSPACE, "batch_t",
                insertCql),
            new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                batchId, DEFAULT_TEST_KEYSPACE, "batch_t",
                insertPrepCql),
            new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                batchId, DEFAULT_TEST_KEYSPACE, "batch_t",
                insertPrepCql)),
        records);
  }

  /** Verifies that driver-level batch request with stale metadata works. */
  @Test
  public void batchWithStaleMetadata() throws Exception {
    {
      session.execute("CREATE TABLE test_batch (h INT, r BIGINT, PRIMARY KEY ((h), r))");
      auditRecords.discard();

      String insertCql = "INSERT INTO test_batch (h, r) VALUES (1, 1)";

      BatchStatement batch = new BatchStatement();
      batch.add(new SimpleStatement("INSERT INTO test_batch (h, r) VALUES (1, 1)"));
      session.execute(batch);

      List<AuditLogEntry> records = auditRecords.popAll();
      String batchId = getBatchId(records);
      assertAuditRecords(
          Arrays.asList(
              new AuditLogEntry('E', "cassandra", "BATCH", "DML",
                  batchId, null /* keyspace */, null /* scope */,
                  "BatchId:[" + batchId + "] - BATCH of [1] statements"),
              new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                  batchId, DEFAULT_TEST_KEYSPACE, "test_batch",
                  insertCql)),
          records);
    }

    {
      session.execute("DROP TABLE test_batch");
      session.execute("CREATE TABLE test_batch (h INT, r TEXT, PRIMARY KEY ((h), r))");
      auditRecords.discard();

      String insertCql = "INSERT INTO test_batch (h, r) VALUES (1, ?)";

      BatchStatement batch = new BatchStatement();
      batch.add(new SimpleStatement(insertCql, "R" + 1));
      session.execute(batch);

      List<AuditLogEntry> records = auditRecords.popAll();
      String batchId = getBatchId(records);
      // TODO: We probably shouldn't log an error here as the user doesn't receive it.
      assertAuditRecords(
          Arrays.asList(
              new AuditLogEntry('E', "cassandra", "BATCH", "DML",
                  batchId, null /* keyspace */, null /* scope */,
                  "BatchId:[" + batchId + "] - BATCH of [1] statements"),
              new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                  batchId, DEFAULT_TEST_KEYSPACE, "test_batch",
                  insertCql),
              new AuditLogEntry('E', "cassandra", "REQUEST_FAILURE", "ERROR",
                  batchId, null /* keyspace */, null /* scope */,
                  insertCql + "; "
                      + "Invalid Arguments. unexpected number byte length: expected 8, provided 2"),
              new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                  batchId, DEFAULT_TEST_KEYSPACE, "test_batch",
                  insertCql)),
          records);
    }
  }

  @Test
  public void dmlAndQuery() throws Exception {
    session.execute("CREATE TABLE t (id int PRIMARY KEY, v text)");
    auditRecords.discard();

    // INSERT
    assertAudit(
        "INSERT INTO t (id, v) VALUES (1, 'v1')",
        (cql) -> Arrays.asList(
            new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
                cql)));

    // INSERT (prepared)
    {
      String insertBindCql = "INSERT INTO t (id, v) VALUES (?, ?)";
      BoundStatement bound = session.prepare(insertBindCql).bind(1, "v1-1");
      assertAudit(
          Collections.nCopies(3 /* One per node */,
              new AuditLogEntry('E', "cassandra", "PREPARE_STATEMENT", "PREPARE",
                  null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
                  insertBindCql)));
      session.execute(bound);
      assertAudit(
          new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
              null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
              insertBindCql));
    }

    // SELECT
    {
      String selectCql = "SELECT * FROM t";
      assertQueryRowsOrdered(selectCql, "Row[1, v1-1]");
      assertAudit(
          new AuditLogEntry('E', "cassandra", "SELECT", "QUERY",
              null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
              selectCql));
    }

    // UPDATE
    assertAudit(
        "UPDATE t SET v = 'v1-2' WHERE id = 1",
        (cql) -> Arrays.asList(
            new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
                cql)));

    // SELECT (prepared)
    {
      String selectCql = "SELECT id, v FROM t WHERE id = ?";
      BoundStatement bound = session.prepare(selectCql).bind(1);
      assertAudit(
          Collections.nCopies(3 /* One per node */,
              new AuditLogEntry('E', "cassandra", "PREPARE_STATEMENT", "PREPARE",
                  null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
                  selectCql)));
      assertQueryRowsOrdered(bound, Arrays.asList("Row[1, v1-2]"));
      assertAudit(
          new AuditLogEntry('E', "cassandra", "SELECT", "QUERY",
              null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
              selectCql));
    }
  }

  @Test
  public void logLevel() throws Exception {
    session.execute("CREATE TABLE t (id int PRIMARY KEY)");
    String cql = "SELECT * FROM t";
    AuditLogEntry entry = new AuditLogEntry('E', "cassandra", "SELECT", "QUERY",
        null /* batchId */, DEFAULT_TEST_KEYSPACE, "t", cql);

    AuditConfig config = new AuditConfig();
    config.enabled = true;

    config.logLevel = "ErroR";
    applyAuditConfig(config);
    entry.logLevel = 'E';
    assertAudit(cql, (__) -> Arrays.asList(entry));

    config.logLevel = "WarninG";
    applyAuditConfig(config);
    entry.logLevel = 'W';
    assertAudit(cql, (__) -> Arrays.asList(entry));

    config.logLevel = "InfO";
    applyAuditConfig(config);
    entry.logLevel = 'I';
    assertAudit(cql, (__) -> Arrays.asList(entry));
  }

  /** Verifies that only requests made to included non-excluded keyspaces should be logged. */
  @Test
  public void filteringByKeyspace() throws Exception {
    AuditConfig config = new AuditConfig();
    config.enabled = true;
    config.includedKeyspaces = "k1,k2";
    config.excludedKeyspaces = "k2,k3";
    applyAuditConfig(config);

    // Keyspace-specific requests, only k1 should be logged.

    for (String ks : Arrays.asList("k1", "k2", "k3")) {
      boolean shouldBeLogged = ks.equals("k1");

      Function<Function<String, AuditLogEntry>,
               Function<String, List<AuditLogEntry>>> generateOneOptionalLog = //
                   (recFn) -> (cql) -> shouldBeLogged
                       ? Arrays.asList(recFn.apply(cql))
                       : Collections.emptyList();

      assertAudit(
          "CREATE KEYSPACE " + ks,
          generateOneOptionalLog.apply(
              (cql) -> new AuditLogEntry('E', "cassandra", "CREATE_KEYSPACE", "DDL",
                  null /* batchId */, ks, null /* scope */,
                  cql)));

      assertAudit(
          "CREATE TABLE " + ks + ".t1 (id int PRIMARY KEY, v text)",
          generateOneOptionalLog.apply(
              (cql) -> new AuditLogEntry('E', "cassandra", "CREATE_TABLE", "DDL",
                  null /* batchId */, ks, "t1",
                  cql)));

      assertAudit(
          "INSERT INTO " + ks + ".t1 (id, v) VALUES (1, 'v1')",
          generateOneOptionalLog.apply(
              (cql) -> new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
                  null /* batchId */, ks, "t1",
                  cql)));

      assertAudit(
          "SELECT * FROM " + ks + ".t1",
          generateOneOptionalLog.apply(
              (cql) -> new AuditLogEntry('E', "cassandra", "SELECT", "QUERY",
                  null /* batchId */, ks, "t1",
                  cql)));

      assertAudit(
          "USE " + ks,
          generateOneOptionalLog.apply(
              (cql) -> new AuditLogEntry('E', "cassandra", "USE_KEYSPACE", "OTHER",
                  null /* batchId */, ks, null /* scope */,
                  cql)),
          false /* excludeUseKeyspace */);

      assertAudit(
          "TRUNCATE t1",
          generateOneOptionalLog.apply(
              (cql) -> new AuditLogEntry('E', "cassandra", "TRUNCATE", "DDL",
                  null /* batchId */, ks, "t1",
                  cql)));

      assertAudit(
          "DROP TABLE t1",
          generateOneOptionalLog.apply(
              (cql) -> new AuditLogEntry('E', "cassandra", "DROP_TABLE", "DDL",
                  null /* batchId */, ks, "t1",
                  cql)));

      assertAudit(
          "DROP KEYSPACE " + ks,
          generateOneOptionalLog.apply(
              (cql) -> new AuditLogEntry('E', "cassandra", "DROP_KEYSPACE", "DDL",
                  null /* batchId */, ks, null /* scope */,
                  cql)));
    }

    // Non keyspace-specific requests, should all be logged.

    assertAudit(
        "CREATE ROLE dummy",
        (cql) -> Arrays.asList(
            new AuditLogEntry('E', "cassandra", "CREATE_ROLE", "DCL",
                null /* batchId */, null /* keyspace */, null /* scope */,
                cql)));
  }

  /** Verifies that only requests made for included non-excluded categories should be logged. */
  @Test
  public void filteringByCategory() throws Exception {
    AuditConfig config = new AuditConfig();
    config.enabled = true;
    config.includedCategories = "DmL,QuErY";
    config.excludedCategories = "qUeRy,dDl";
    applyAuditConfig(config);

    // Only DML is logged.
    String ddl = "CREATE TABLE t (id int PRIMARY KEY, v text)";
    String dml = "INSERT INTO t (id, v) VALUES (1, 'a')";
    String query = "SELECT * FROM t";
    session.execute(ddl);
    session.execute(dml);
    session.execute(query);
    assertAudit(
        new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
            null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
            dml));
  }

  /** Verifies that only requests made by included non-excluded users should be logged. */
  @Test
  public void filteringByUser() throws Exception {
    AuditConfig config = new AuditConfig();
    config.enabled = true;
    config.includedUsers = "UseR1,UsEr2";
    config.excludedUsers = "uSeR2,uSEr3";
    applyAuditConfig(config);

    session.execute("CREATE ROLE user2 WITH login = true AND password = '123'");
    session.execute("CREATE ROLE user3 WITH login = true AND password = '123'");
    session.execute("GRANT ALL PERMISSIONS on KEYSPACE " + DEFAULT_TEST_KEYSPACE + " to user1");
    session.execute("GRANT ALL PERMISSIONS on KEYSPACE " + DEFAULT_TEST_KEYSPACE + " to user2");
    session.execute("GRANT ALL PERMISSIONS on KEYSPACE " + DEFAULT_TEST_KEYSPACE + " to user3");
    session.execute("CREATE TABLE t (id int PRIMARY KEY)");
    auditRecords.discard();

    String cql = "SELECT * FROM " + DEFAULT_TEST_KEYSPACE + ".t";

    try (Cluster cluster = getCluster("user1", "123");
         Session session = cluster.connect()) {
      session.execute(cql);
      // 1) There are 4 login events: one connection per node + a control connection.
      // 2) Due to some driver version error, control connection issues an erroneous query
      //    "SELECT * FROM system.peers_v2" for a table that doesn't exist.
      //    We don't care about it here, so we filter it out.
      AuditLogEntry loginRecord = new AuditLogEntry('E', "user1", "LOGIN_SUCCESS", "AUTH",
          null /* batchId */, null /* keyspace */, null /* scope */,
          "LOGIN SUCCESSFUL");
      List<AuditLogEntry> actualRecords = auditRecords.popAll().stream()
          .filter(e -> e.operationAndErrorMessage == null
              || !e.operationAndErrorMessage.contains("peers_v2"))
          .collect(Collectors.toList());
      assertAuditRecords(
          Arrays.asList(
              loginRecord,
              loginRecord,
              loginRecord,
              loginRecord,
              new AuditLogEntry('E', "user1", "SELECT", "QUERY",
                  null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
                  cql)),
          actualRecords);
    }

    try (Cluster cluster = getCluster("user2", "123");
         Session session = cluster.connect()) {
      session.execute(cql);
      assertAudit(Collections.emptyList());
    }

    try (Cluster cluster = getCluster("user3", "123");
         Session session = cluster.connect()) {
      session.execute(cql);
      assertAudit(Collections.emptyList());
    }
  }

  @Test
  public void timing() throws Exception {
    for (String cql : Arrays.asList(
        "CREATE TABLE t (id int PRIMARY KEY, v int)",
        "INSERT INTO t (id, v) VALUES (1, 1)",
        "SELECT * FROM t")) {
      long beforeMs = System.currentTimeMillis();
      session.execute(cql);
      long afterMs = System.currentTimeMillis();

      List<AuditLogEntry> records = auditRecords.popAll();
      assertTrue("No audit was logged for " + cql, records.size() >= 1);
      for (AuditLogEntry record : records) {
        long recordTs = Long.parseLong(record.timestamp);
        assertGreaterThanOrEqualTo(
            "Timestamp for record " + record + " comes before the statement was issued!",
            recordTs, beforeMs);
        assertLessThanOrEqualTo(
            "Timestamp for record " + record + " comes after the statement has returned!",
            recordTs, afterMs);
      }
    }
  }

  @Test
  public void errors() throws Exception {
    session.execute("CREATE TABLE t (id int PRIMARY KEY, v int)"
        + " WITH transactions = { 'enabled': true }");
    session.execute("CREATE UNIQUE INDEX ON t (v)");

    waitForReadPermsOnAllIndexes("t");

    session.execute("INSERT INTO t (id, v) VALUES (1, 1)");
    auditRecords.discard();

    // Parse error
    {
      String cql = "ZELEGT * FROUME t";
      String expectedError = "Invalid SQL Statement."
          + " syntax error, unexpected IDENT, expecting end_of_file";
      assertQueryError(cql, expectedError);
      assertAudit(
          new AuditLogEntry('E', "cassandra", "REQUEST_FAILURE", "ERROR",
              null /* batchId */, null /* keyspace */, null /* scope */,
              cql + "; " + expectedError));
    }

    // Analyze error
    {
      String cql = "SELECT v2 FROM t";
      String expectedError = "Undefined Column. Column doesn't exist";
      assertQueryError(cql, expectedError);
      assertAudit(
          new AuditLogEntry('E', "cassandra", "REQUEST_FAILURE", "ERROR",
              null /* batchId */, null /* keyspace */, null /* scope */,
              cql + "; " + expectedError));
    }

    // Execution error
    {
      String cql = "INSERT INTO t (id, v) VALUES (2, 1)";
      String expectedError = "Execution Error. Duplicate value disallowed by unique index t_v_idx";
      assertQueryError(cql, expectedError);
      assertAudit(
          new AuditLogEntry('E', "cassandra", "UPDATE", "DML",
              null /* batchId */, DEFAULT_TEST_KEYSPACE, "t",
              cql),
          new AuditLogEntry('E', "cassandra", "REQUEST_FAILURE", "ERROR",
              null /* batchId */, null /* keyspace */, null /* scope */,
              cql + "; " + expectedError));
    }
  }

  //
  // Helpers
  //

  /**
   * Create a "cluster" to connect with the given credentials. Note that while returning
   * {@code Session} would be more convenient, we can't do that because cluster needs to be closed.
   */
  private Cluster getCluster(String username, String password) {
    return getDefaultClusterBuilder().withCredentials(username, password).build();
  }

  private String getBatchId(List<AuditLogEntry> records) throws Exception {
    Optional<String> batchIdOpt = records.stream()
        .filter(e -> e.type.equals("BATCH")).map(e -> e.batchId).findFirst();
    assertTrue("No BATCH requests were present! Audit: " + records, batchIdOpt.isPresent());
    return batchIdOpt.get();
  }

  private void runProcess(List<String> args) throws Exception {
    assertEquals(0, new ProcessBuilder(args).start().waitFor());
  }

  /**
   * Applies non-null values from the given audit config to all tservers. Null values are not
   * changed.
   */
  private void applyAuditConfig(AuditConfig config) throws Exception {
    StopWatch sw = StopWatch.createStarted();
    List<String> processArgs = Arrays.asList(
        TestUtils.findBinary("yb-ts-cli"),
        "--server_address",
        "<PLACEHOLDER>",
        "set_flag",
        "<PLACEHOLDER>",
        "<PLACEHOLDER>");
    for (HostAndPort server : miniCluster.getTabletServers().keySet()) {
      processArgs.set(2, server.toString());
      for (Entry<String, String> entry : config.getGflags().entrySet()) {
        processArgs.set(4, entry.getKey());
        processArgs.set(5, entry.getValue());
        runProcess(processArgs);
      }
    }
    LOG.info("Audit config applied in " + sw.getTime() + " ms");
  }

  //
  // Audit assertion helpers.
  // Note that they filter out USE_KEYSPACE by default.
  //

  private void assertAudit(AuditLogEntry... log)
      throws Exception {
    assertAudit(Arrays.<AuditLogEntry>asList(log));
  }

  private void assertAudit(List<AuditLogEntry> log)
      throws Exception {
    assertAuditRecords(log, auditRecords.popAll());
  }

  private void assertAudit(
      String cql,
      Function<String, List<AuditLogEntry>> generateLog) throws Exception {
    assertAudit(cql, generateLog, true /* excludeUseKeyspace */);
  }

  private void assertAudit(
      String cql,
      Function<String, List<AuditLogEntry>> generateLog,
      boolean excludeUseKeyspace) throws Exception {
    auditRecords.discard();
    session.execute(cql);
    assertAuditRecords("Audit mismatch for " + cql + ";\n", generateLog.apply(cql),
        auditRecords.popAll(), excludeUseKeyspace);
  }

  private void assertAuditRecords(List<AuditLogEntry> expected, List<AuditLogEntry> actual) {
    assertAuditRecords("", expected, actual, true /* excludeUseKeyspace */);
  }

  /**
   * @param excludeUseKeyspace
   *          The first operation after {@code USE ks} triggers an additional log entry
   *          {@code USE "ks"} on every node current connection goes to. This is expected and aligns
   *          with Cassandra, but not convenient to test.
   */
  private void assertAuditRecords(
      String prefixMsg,
      List<AuditLogEntry> expected,
      List<AuditLogEntry> actual,
      boolean excludeUseKeyspace) {
    List<AuditLogEntry> actual2 = actual.stream()
        .filter(e -> !excludeUseKeyspace || !e.type.equals("USE_KEYSPACE"))
        .collect(Collectors.toList());
    if (!Objects.equals(expected, actual2)) {
      fail(prefixMsg + String.format("expected:<\n  %s\n> but was:<\n  %s\n>",
          StringUtils.joinWith("\n  ", expected.toArray()),
          StringUtils.joinWith("\n  ", actual2.toArray())));
    }
  }

  /**
   * Helper class for audit gflag configuration.
   * <p>
   * Null values mean that gflag should be removed.
   */
  private static class AuditConfig {
    public boolean enabled;
    public String logLevel;
    public String includedKeyspaces;
    public String excludedKeyspaces;
    public String includedCategories;
    public String excludedCategories;
    public String includedUsers;
    public String excludedUsers;

    public Map<String, String> getGflags() {
      Map<String, String> map = new LinkedHashMap<>();
      map.put("ycql_enable_audit_log", Boolean.toString(enabled));
      map.put("ycql_audit_log_level", logLevel);
      map.put("ycql_audit_included_keyspaces", includedKeyspaces);
      map.put("ycql_audit_excluded_keyspaces", excludedKeyspaces);
      map.put("ycql_audit_included_categories", includedCategories);
      map.put("ycql_audit_excluded_categories", excludedCategories);
      map.put("ycql_audit_included_users", includedUsers);
      map.put("ycql_audit_excluded_users", excludedUsers);
      map.values().removeIf(v -> v == null);
      return map;
    }
  }

  /** Sink for audit records from all tservers. */
  private static class CurrentAuditRecords {
    private final List<AuditLogEntry> storage = Collections
        .synchronizedList(new ArrayList<>());

    public void push(AuditLogEntry entry) {
      storage.add(entry);
    }

    /** Discard existing audit records so that only newer ones will be retrieved. */
    public void discard() throws Exception {
      popAll();
    }

    /** Retrieve the audit records added to the log since last call, discarding them. */
    public List<AuditLogEntry> popAll() throws Exception {
      Thread.sleep(BuildTypeUtil.adjustTimeout(200));
      synchronized (storage) {
        List<AuditLogEntry> result = new ArrayList<>(storage);
        storage.clear();
        return result;
      }
    }
  }

  /** Listens to log file on a single tserver, adding audit records to {@code auditRecords}. */
  private static class AuditLogListener implements LogErrorListener {
    private final String PREFIX = "AUDIT: ";

    private final int tserverPort;
    private final CurrentAuditRecords auditRecords;

    public AuditLogListener(int tserverPort, CurrentAuditRecords auditRecords) {
      this.tserverPort = tserverPort;
      this.auditRecords = auditRecords;
    }

    @Override
    public void handleLine(String line) {
      int idx = line.indexOf(PREFIX);
      if (idx == -1) {
        return;
      }

      AuditLogEntry entry = new AuditLogEntry();
      entry.tserverPort = tserverPort;
      entry.logLevel = line.charAt(0);

      // This would slip if an operation contains "|" character but we just won't issue those.
      String rest = line.substring(idx + PREFIX.length());
      for (String part : rest.split("\\|")) {
        String[] partsOfPart = part.split(":", 2);
        switch (partsOfPart[0]) {
          case "user":
            entry.user = partsOfPart[1];
            break;
          case "host":
            entry.host = partsOfPart[1];
            break;
          case "source":
            entry.source = partsOfPart[1];
            break;
          case "port":
            entry.source += ":" + partsOfPart[1];
            break;
          case "timestamp":
            entry.timestamp = partsOfPart[1];
            break;
          case "type":
            entry.type = partsOfPart[1];
            break;
          case "category":
            entry.category = partsOfPart[1];
            break;
          case "batch":
            entry.batchId = partsOfPart[1];
            break;
          case "ks":
            entry.keyspace = partsOfPart[1];
            break;
          case "scope":
            entry.scope = partsOfPart[1];
            break;
          case "operation":
            entry.operationAndErrorMessage = partsOfPart[1];
            break;
          default:
            throw new IllegalStateException("Unexpected prefix " + partsOfPart[0]
                + ", line: " + line);
        }
      }
      auditRecords.push(entry);
    }

    @Override
    public void reportErrorsAtEnd() {
      // NOOP
    }
  }

  private static class AuditLogEntry {
    public Character logLevel;
    public int tserverPort;
    public String user;
    public String host;
    public String source;
    public String timestamp;
    public String type;
    public String category;
    public String batchId;
    public String keyspace;
    public String scope;
    public String operationAndErrorMessage;

    public AuditLogEntry() {
    }

    // Auto-generated boilerplate.
    // (Ignores tserverPort, host, source and timestamp.)

    public AuditLogEntry(Character logLevel, String user, String type, String category,
        String batchId, String keyspace, String scope, String operationAndErrorMessage) {
      this.logLevel = logLevel;
      this.user = user;
      this.type = type;
      this.category = category;
      this.batchId = batchId;
      this.keyspace = keyspace;
      this.scope = scope;
      this.operationAndErrorMessage = operationAndErrorMessage;
    }

    @Override
    public int hashCode() {
      return Objects.hash(batchId, category, keyspace, logLevel, operationAndErrorMessage, scope,
          type, user);
    }

    @Override
    public boolean equals(Object obj) {
      if (this == obj) return true;
      if (obj == null) return false;
      if (getClass() != obj.getClass()) return false;
      AuditLogEntry other = (AuditLogEntry) obj;
      return Objects.equals(batchId, other.batchId) && Objects.equals(category, other.category)
          && Objects.equals(keyspace, other.keyspace) && Objects.equals(logLevel, other.logLevel)
          && Objects.equals(operationAndErrorMessage, other.operationAndErrorMessage)
          && Objects.equals(scope, other.scope) && Objects.equals(type, other.type)
          && Objects.equals(user, other.user);
    }

    @Override
    public String toString() {
      return "AuditLogEntry [logLevel=" + logLevel + ", user=" + user + ", type=" + type
          + ", category=" + category + ", batchId=" + batchId + ", keyspace=" + keyspace
          + ", scope=" + scope + ", operationAndErrorMessage=" + operationAndErrorMessage + "]";
    }
  }
}
