// Copyright (c) YugabyteDB, Inc.
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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;
import static org.yb.AssertionWrappers.*;
import static org.yb.pgsql.ExplainAnalyzeUtils.getExplainQueryId;

/**
 * Runs tests for tests query id stability, including queries
 * with comments, hints, and different spacing.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestYbQueryId extends BasePgSQLTest {

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  /**
   * Try combinations of GUCs related to hinting to make sure query ids
   * are stable across values. Each query should get the same query id
   * regardless of GUC values, or the presence/absence comments or hints.
   *
   * This test runs the set of queries twice. The first run is with
   * hint table caching on (default) and the second with hint table caching off.
   * On both runs we collect the query ids for the queries. On the second run
   * we assert the second run query id should be the same as that seen in
   * the first run for queries we expect query id stability for across
   * cluster destruction/creation.
   *
   * @throws Exception
   */
  @Test
  public void testYbQueryId1() throws Exception {
    final String[] queryArr = {"SELECT * FROM t1, t2 WHERE a1=a2 AND a1<5",
      "/*+ SeqScan(t2) */ SELECT * FROM t1, t2 WHERE a1=a2 AND a2>2",
      "SELECT * FROM t1, t2 WHERE a1=a2 AND a2>2 /*+ MergeJoin(t1 t2) */",
      "select * from information_schema.tables",
      "select   *       from                information_schema.tables   ",
      "/*+ set(yb_enable_batchednl false) */ select * from information_schema.columns",
      "select * from information_schema.columns /*+ set(yb_enable_batchednl false) */"};

    long[][] queryIdArr = {{0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}};

    for (int runId = 0; runId < 2; ++runId) {
      Statement stmt = connection.createStatement();
      stmt.execute("create table t1(a1 int, primary key(a1 asc))");
      stmt.execute("create table t2(a2 int, primary key(a2 desc))");
      stmt.execute("create extension if not exists pg_hint_plan");

      int queryIndex = 0;
      for (String query : queryArr) {
        try (Statement stmt1 = connection.createStatement()) {
          stmt1.execute("RESET ALL");

          long queryId1 = getExplainQueryId(stmt1, query);
          queryIdArr[runId][queryIndex] = queryId1;

          stmt1.execute("SET pg_hint_plan.enable_hint = TRUE");
          long queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET pg_hint_plan.hints_anywhere TO ON");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET pg_hint_plan.enable_hint_table = TRUE");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET yb_enable_base_scans_cost_model = TRUE");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET yb_enable_optimizer_statistics = TRUE");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET pg_hint_plan.yb_use_query_id_for_hinting = TRUE");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          /*
          * Place comment in front of the query.
          */
          query = "/* comment */ " + query;
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET pg_hint_plan.hints_anywhere TO ON");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("RESET ALL");
          stmt1.close();
        }

        ++queryIndex;
      }

      // Queries 3 and 4 are the same except for spaces. Queries 5 and 6
      // are the same also except for hint placement.
      assertEquals(queryIdArr[runId][3], queryIdArr[runId][4]);
      assertEquals(queryIdArr[runId][5], queryIdArr[runId][6]);

      if (runId == 1) {
        // Queries 3, 4, 5, and 6 should have stable query ids across runs
        // since they go against tables whose internal ids should not vary
        // across cluster restart.
        assertEquals(queryIdArr[0][3], queryIdArr[1][3]);
        assertEquals(queryIdArr[0][4], queryIdArr[1][4]);
        assertEquals(queryIdArr[0][5], queryIdArr[1][5]);
        assertEquals(queryIdArr[0][6], queryIdArr[1][6]);
      } else {
        // Set up the server flags for restart for the second run.
        Map<String, String> flagMap = super.getTServerFlags();
        appendToYsqlPgConf(flagMap, "pg_hint_plan.yb_enable_hint_table_cache=off");
        restartClusterWithFlags(Collections.emptyMap(), flagMap);
      }
    }
  }

  public long getUserId(Statement stmt, String userName) throws SQLException {
    long userId = 0;
    String sql = String.format(
    "SELECT oid FROM pg_roles WHERE rolname = %s", userName);
    try (ResultSet rs = stmt.executeQuery(sql)) {

      int cnt = 0;
      while (rs.next()) {
        userId = rs.getLong("oid");
        ++cnt;
      }

      assertEquals(cnt, 1);
    }

    return userId;
  }

  // Create a list from a result set.
  public List<List<Object>> resultSetToList(ResultSet rs) throws SQLException {
    List<List<Object>> rows = new ArrayList<>();
    ResultSetMetaData md = rs.getMetaData();
    int cols = md.getColumnCount();

    while (rs.next()) {
      List<Object> row = new ArrayList<>();
      for (int i = 1; i <= cols; i++) {
        row.add(rs.getObject(i));
      }

      rows.add(row);
    }
    return rows;
  }

  // Get DB OID from a DB name.
  public long getDatabaseId(Statement stmt, String dbName) throws SQLException {
    long userId = 0;
    String sql = String.format("SELECT oid FROM pg_database WHERE datname = '%s'", dbName);
    try (ResultSet rs = stmt.executeQuery(sql)) {

      int cnt = 0;
      while (rs.next()) {
        userId = rs.getLong("oid");
        ++cnt;
      }

      assertEquals(cnt, 1);
    }

    return userId;
  }

  /**
   * Create 1 table in each of 3 new databases. Make sure the query ids match.
   * Execute queries as super user and regular user(s) and check what is stored
   * in pg_stat_statements.
   *
   * @throws Exception
   */
  @Test
  public void testYbQueryIdAcrossDatabases() throws Exception {
    Statement suStmt = connection.createStatement();

    // Create 3 databases.
    suStmt.execute("create database db1");
    suStmt.execute("create database db2");
    suStmt.execute("create database db3");

    // Get the DB OIDs.
    long dbId1 = getDatabaseId(suStmt, "db1");
    long dbId2 = getDatabaseId(suStmt, "db2");
    long dbId3 = getDatabaseId(suStmt, "db3");

    List<List<Object>> expectedEntryList = new ArrayList<>();

    // Super user connection.
    Connection suConnection1 = getConnectionBuilder().withDatabase("db1").connect();
    Statement suStmt1 = suConnection1.createStatement();
    suStmt1.execute("SELECT pg_stat_statements_reset()");
    suStmt1.execute("CREATE TABLE t1(a1 INT, PRIMARY KEY(a1 ASC))");
    suStmt1.execute("INSERT INTO t1 VALUES(1)");

    // Get the query id.
    long queryId1 = getExplainQueryId(suStmt1, "SELECT a1 FROM t1");

    // Execute the query and check for expected values.
    try (ResultSet rs = suStmt1.executeQuery("/* DB1 as user1 */ SELECT a1 FROM t1")) {

      int cnt = 0;
      while (rs.next()) {
        int a1 = rs.getInt("a1");
        assertEquals(a1, 1);
        ++cnt;
      }

      assertEquals(cnt, 1);
    }

    // Get super user id.
    long suId = getUserId(suStmt1, "current_user") ;

    // Add an expected entry.
    expectedEntryList.add(Arrays.asList(dbId1, queryId1, suId));

    // Create a regular user.
    suStmt1.execute("CREATE ROLE user1 LOGIN PASSWORD 'password123'");
    suStmt1.execute("GRANT ALL ON TABLE t1 TO user1");

    Connection user1Connection = getConnectionBuilder().withDatabase("db1").
                          withUser("user1").withPassword("password123").connect();
    Statement user1Stmt = user1Connection.createStatement();

    // Get user id.
    long user1Id = getUserId(user1Stmt, "current_user") ;

    // Execute query and do checks.
    try (ResultSet rs = user1Stmt.executeQuery("/* DB1 as user1 */ SELECT a1 FROM t1")) {

      int cnt = 0;
      while (rs.next()) {
        int a1 = rs.getInt("a1");
        assertEquals(a1, 1);
        ++cnt;
      }

      assertEquals(cnt, 1);
    }

    expectedEntryList.add(Arrays.asList(dbId1, queryId1, user1Id));

    Connection suConnection2 = getConnectionBuilder().withDatabase("db2").connect();
    Statement suStmt2 = suConnection2.createStatement();
    suStmt2.execute("CREATE TABLE t1(a1 INT, PRIMARY KEY(a1 ASC))");
    suStmt2.execute("INSERT INTO t1 VALUES(2)");
    long queryId2 = getExplainQueryId(suStmt2, "/* DB2 as super_user */ SELECT a1 FROM t1");

    try (ResultSet rs = suStmt2.executeQuery("/* DB2 as super_user */ SELECT a1 FROM t1")) {

      int cnt = 0;
      while (rs.next()) {
        int a1 = rs.getInt("a1");
        assertEquals(a1, 2);
        ++cnt;
      }

      assertEquals(cnt, 1);
    }

    expectedEntryList.add(Arrays.asList(dbId2, queryId2, suId));

    // Query ids should match since the OIDs of the table are the same and the single
    // Var "a1" will have the same internal identifiers; query id does not use
    // Var names.
    assertEquals(queryId1, queryId2);

    Connection suConnection3 = getConnectionBuilder().withDatabase("db3").connect();
    Statement suStmt3 = suConnection3.createStatement();
    suStmt3.execute("CREATE TABLE t2(a2 INT, PRIMARY KEY(a2 asc))");
    suStmt3.execute("INSERT INTO t2 VALUES(3)");
    long queryId3 = getExplainQueryId(suStmt3, "/* DB3 as super_user */ SELECT a2 FROM t2");

    try (ResultSet rs = suStmt3.executeQuery("/* DB3 as super_user */ SELECT a2 FROM t2")) {

      int cnt = 0;
      while (rs.next()) {
        int a2 = rs.getInt("a2");
        assertEquals(a2, 3);
        ++cnt;
      }

      assertEquals(cnt, 1);
    }

    expectedEntryList.add(Arrays.asList(dbId3, queryId3, suId));

    // Table OIDs and the single Vars "a1" and "a2" will have the same identifiers.
    assertEquals(queryId2, queryId3);

    suStmt3.execute("CREATE ROLE user2 LOGIN PASSWORD 'password123'");
    suStmt3.execute("GRANT ALL ON TABLE t2 TO user2");

    Connection user2Connection = getConnectionBuilder().withDatabase("db3").
                          withUser("user2").withPassword("password123").connect();
    Statement user2Stmt = user2Connection.createStatement();

    long user2Id = getUserId(user2Stmt, "current_user") ;

    try (ResultSet rs = user2Stmt.executeQuery("/* DB3 as user2 */ SELECT a2 FROM t2")) {

      int cnt = 0;
      while (rs.next()) {
        int a2 = rs.getInt("a2");
        assertEquals(a2, 3);
        ++cnt;
      }

      assertEquals(cnt, 1);
    }

    expectedEntryList.add(Arrays.asList(dbId3, queryId3, user2Id));

    String pgStatsQuery1
      = String.format("SELECT dbid, queryid, userid FROM pg_stat_statements " +
                      "WHERE queryid IN (%d, %d, %d)",
                      queryId1, queryId2, queryId3);

    List<List<Object>> suRsList;
    try (ResultSet rs = suStmt.executeQuery(pgStatsQuery1)) {
      suRsList = resultSetToList(rs);
    }

    // Should throw an error since user1 does not have permission to read stats.
    assertThrows(SQLException.class, () -> {
        user1Stmt.executeQuery(pgStatsQuery1);
    });

    suStmt.execute("GRANT pg_read_all_stats TO user1");

    List<List<Object>> user1RsList;
    try (ResultSet rs = user1Stmt.executeQuery(pgStatsQuery1)) {
      user1RsList = resultSetToList(rs);
    }

    // Super user and user 1 should see the same entries.
    assertEquals(suRsList, user1RsList);

    suStmt.execute("GRANT pg_read_all_stats TO user2");

    List<List<Object>> user2RsList;
    try (ResultSet rs = user1Stmt.executeQuery(pgStatsQuery1)) {
      user2RsList = resultSetToList(rs);
    }

    // Super user and user 2 should see the same entries.
    assertEquals(suRsList, user2RsList);

    // Compare expected entries with ones super user saw. Do an unordered
    // comparison.
    assertEquals(new HashSet<>(suRsList), new HashSet<>(expectedEntryList));
  }
}
