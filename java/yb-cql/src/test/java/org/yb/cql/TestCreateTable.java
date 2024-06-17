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

import java.util.HashMap;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.util.Set;
import java.util.Map;
import java.util.HashMap;

import org.yb.YBTestRunner;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestCreateTable extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestCreateTable.class);

  @Test
  public void testCreateTable() throws Exception {
    LOG.info("Begin test");

    // Test create table multiple times (ENG-945)
    for (int i = 0; i < 5; i++) {

      // Create table
      String create_stmt = "CREATE TABLE test_create " +
                           "(h1 int, h2 varchar, r1 int, r2 varchar, v1 int, v2 varchar, " +
                           "primary key ((h1, h2), r1, r2));";
      session.execute(create_stmt);

      // Insert one row. Deliberately insert with same hash key but different range column values.
      String insert_stmt = String.format("INSERT INTO test_create (h1, h2, r1, r2, v1, v2) " +
                                         "VALUES (1, 'a', %s, 'b', %s, 'c');", i, i + 1);
      session.execute(insert_stmt);

      // Select row by the hash key from the test table. Expect only one row returned and not
      // any from previous instances of the table.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_create " +
                           "WHERE h1 = 1 AND h2 = 'a';";
      ResultSet rs = session.execute(select_stmt);
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals("a", row.getString("h2"));
      assertEquals(i, row.getInt("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(i + 1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
      assertNull(rs.one());

      String drop_stmt = "DROP TABLE test_create;";
      session.execute(drop_stmt);
    }

    LOG.info("End test");
  }

  // This is the same as testCreateTable, but it defines the key before the columns.
  @Test
  public void testCreateTableDefineKeyFirst() throws Exception {
    LOG.info("Begin test");

    // Test create table multiple times (ENG-945)
    for (int i = 0; i < 5; i++) {

      // Create table
      String create_stmt = "CREATE TABLE test_create " +
                           "(h1 int, r1 int, v1 int, primary key ((h1, h2), r1, r2), " +
                           " h2 varchar, r2 varchar, v2 varchar);";
      session.execute(create_stmt);

      // Insert one row. Deliberately insert with same hash key but different range column values.
      String insert_stmt = String.format("INSERT INTO test_create (h1, h2, r1, r2, v1, v2) " +
                                         "VALUES (1, 'a', %s, 'b', %s, 'c');", i, i + 1);
      session.execute(insert_stmt);

      // Select row by the hash key from the test table. Expect only one row returned and not
      // any from previous instances of the table.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_create " +
                           "WHERE h1 = 1 AND h2 = 'a';";
      ResultSet rs = session.execute(select_stmt);
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals("a", row.getString("h2"));
      assertEquals(i, row.getInt("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(i + 1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
      assertNull(rs.one());

      String drop_stmt = "DROP TABLE test_create;";
      session.execute(drop_stmt);
    }

    LOG.info("End test");
  }

  @Test
  public void testCreateTableWithAllDatatypes() throws Exception {
    LOG.info("Begin test");

    // Create table
    String create_stmt = "CREATE TABLE test_create " +
                         "(c1 tinyint, c2 smallint, c3 integer, c4 bigint, " +
                         "c5 float, c6 double, " +
                         "c7 varchar, " +
                         "c8 boolean, " +
                         "c9 timestamp, " +
                         "c10 inet, " +
                         "c11 uuid, " +
                         "c12 timeuuid, " +
                         "c13 jsonb, " +
                         "primary key (c1));";
    session.execute(create_stmt);

    String drop_stmt = "DROP TABLE test_create;";
    session.execute(drop_stmt);

    LOG.info("End test");
  }

  public void testCreateTableWithColumnNamedKeyword(String keyword) throws Exception {
    // This test is to check if the specified <keyword> is allowed to be used as column
    // in various syntax. This test does not need to verify correctness of data values.

    // Create table with as <keyword> as key column.
    session.execute("CREATE TABLE test_create_keyword_key (" +
                    keyword + " int, c1 double, c2 varchar, primary key (" + keyword + "));");
    session.execute("INSERT INTO test_create_keyword_key (" + keyword + ") VALUES (1);");
    session.execute("INSERT INTO test_create_keyword_key (c1, " +
                    keyword + ", c2) VALUES (2.2, 2, '2');");
    session.execute("SELECT " + keyword + " FROM test_create_keyword_key WHERE " +
                    keyword + " = 1;");
    session.execute("SELECT " + keyword + ", c1, c2 FROM test_create_keyword_key WHERE " +
                    keyword + " = 2;");
    session.execute("DROP TABLE test_create_keyword_key;");

    // Create table with as <keyword> as range column.
    session.execute("CREATE TABLE test_create_keyword_rng (c1 int, " +
                    keyword + " double, c2 varchar, primary key (c1, " + keyword + "));");
    session.execute("SELECT c1, " + keyword + ", c2 FROM test_create_keyword_rng WHERE c1 = 1 " +
                    "ORDER BY " + keyword + ";");
    session.execute("DROP TABLE test_create_keyword_rng;");

    // Create table with as <keyword> as counter.
    session.execute("CREATE TABLE test_create_keyword_cnt (c1 int, " +
                    keyword + " counter, c2 counter, primary key (c1));");
    session.execute("UPDATE test_create_keyword_cnt SET " + keyword + " = " +
                    keyword + " + 1 WHERE c1 = 1;");
    session.execute("DROP TABLE test_create_keyword_cnt;");

    // Create table with as <keyword> as regular column, plus create an index on this column.
    session.execute("CREATE TABLE test_create_keyword_reg (c1 int, " +
                    keyword + " double, c2 varchar, primary key (c1)) " +
                    "WITH transactions = { 'enabled' : true };");
    session.execute("CREATE INDEX ON test_create_keyword_reg(" + keyword + ");");

    waitForReadPermsOnAllIndexes("test_create_keyword_reg");

    session.execute("SELECT c1, " + keyword + ", c2 FROM test_create_keyword_reg;");
    session.execute("UPDATE test_create_keyword_reg SET " + keyword + " = 3 WHERE c1 = 2;");
    session.execute("DROP TABLE test_create_keyword_reg;");
  }

  @Test
  public void testCreateTableWithColumnNamedOffset() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());
    testCreateTableWithColumnNamedKeyword("offset");
    LOG.info("End test: " + getCurrentTestMethodName());
  }

  @Test
  public void testCreateTableWithColumnNamedGroup() throws Exception {
    // Run test with error suppressed as GROUP BY is not supported.
    Map<String, String> flags = new HashMap<>();
    flags.put("ycql_suppress_group_by_error", "true");
    restartClusterWithTSFlags(flags);
    LOG.info("Start test: " + getCurrentTestMethodName());
    testCreateTableWithColumnNamedKeyword("group");

    session.execute("CREATE TABLE test_tbl (id int primary key, v int, group int);");
    session.execute("SELECT * FROM test_tbl GROUP BY id;");
    session.execute("SELECT * FROM test_tbl GROUP BY v;");
    session.execute("SELECT * FROM test_tbl GROUP BY group;");
    runInvalidStmt("SELECT * FROM test_tbl GROUP v;");
    session.execute("DROP TABLE test_tbl;");

    LOG.info("End test: " + getCurrentTestMethodName());
  }

  @Test
  public void testCreateTableSystemNamespace() throws Exception {
    runInvalidStmt("CREATE TABLE system.abc (c1 int, PRIMARY KEY(c1));");
  }

  @Test
  public void testCreateTableNumTablets() throws Exception {
    restartClusterWithTSFlags(new HashMap<String, String>());
    
    // Test default number of tablets.
    session.execute("CREATE TABLE test_num_tablets_1 (id int PRIMARY KEY);");
    Set<String> ids =
      miniCluster.getClient().getTabletUUIDs(DEFAULT_TEST_KEYSPACE, "test_num_tablets_1");
    assertEquals(ids.size(), NUM_TABLET_SERVERS * getNumShardsPerTServer());

    // Test with tablets table property set.
    session.execute("CREATE TABLE test_num_tablets_2 (id int PRIMARY KEY) WITH tablets = 10;");
    ids = miniCluster.getClient().getTabletUUIDs(DEFAULT_TEST_KEYSPACE, "test_num_tablets_2");
    assertEquals(ids.size(), 10);

    // Test with tablets and transations table properties set.
    assertFalse(miniCluster.getClient().tableExists("system", "transactions"));
    session.execute(
        "CREATE TABLE test_num_tablets_3 (id int PRIMARY KEY) WITH tablets = 10 " +
        "AND transactions = { 'enabled' : true };");
    ids = miniCluster.getClient().getTabletUUIDs(DEFAULT_TEST_KEYSPACE, "test_num_tablets_3");
    assertEquals(ids.size(), 10);
    assertTrue(miniCluster.getClient().tableExists("system", "transactions"));
    // Test index table with tablets table property set.
    session.execute("CREATE INDEX on test_num_tablets_3 (id) WITH tablets = 5;");

    waitForReadPermsOnAllIndexes("test_num_tablets_3");

    ids =
      miniCluster.getClient().getTabletUUIDs(DEFAULT_TEST_KEYSPACE, "test_num_tablets_3_id_idx");
    assertEquals(ids.size(), 5);

    // Test with number of tablets exceeding the limit.
    assertQueryError("CREATE TABLE test_num_tablets_4 (id int PRIMARY KEY) WITH tablets = 50000;",
        "Number of tablets exceeds system limit");
  }
}
