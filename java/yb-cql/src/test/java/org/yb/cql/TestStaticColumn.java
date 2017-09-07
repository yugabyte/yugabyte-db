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

import java.util.*;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class TestStaticColumn extends BaseCQLTest {

  private void createTable(boolean insertSeedData) {
    session.execute("create table t (" +
                    "h1 int, h2 varchar, " +
                    "r1 int, r2 varchar, " +
                    "s1 int static, s2 varchar static, " +
                    "c1 int, c2 varchar, " +
                    "primary key ((h1, h2), r1, r2));");
    if (!insertSeedData)
      return;

    // Insert rows. 3 hash keys with 3 range keys each. The static column is updated with each
    // range key so we should see the same last value across the range values at the end.
    {
      PreparedStatement stmt = session.prepare("insert into t (h1, h2, r1, r2, s1, s2, c1, c2) "+
                                               "values (?, ?, ?, ?, ?, ?, ?, ?);");
      for (int h = 1; h <= 3; h++) {
        for (int r = 1; r <= 3; r++) {
          session.execute(stmt.bind(new Integer(h), "h" + h,
                                    new Integer(r), "r" + r,
                                    new Integer(h*10+r), "s" + (h*10+r), // static columns s1 and s2
                                    new Integer(r), "c" + r // non-static columns c1 and c2
                                    ));
        }
      }
    }

    // Insert 3 other rows with static columns using hash key only.
    {
      PreparedStatement stmt = session.prepare("insert into t (h1, h2, s1, s2) "+
                                               "values (?, ?, ?, ?);");
      for (int h = 4; h <= 6; h++) {
        session.execute(stmt.bind(new Integer(h), "h" + h,
                                  new Integer(h*10), "s" + (h*10) // static columns s1 and s2
                                  ));
      }
    }
  }

  @Test
  public void testCreateTable() throws Exception {
    LOG.info("Test Start");

    // Test create table with static column.
    session.execute("create table t (h int, r int, s int static, PRIMARY KEY((h), r));");
    session.execute("drop table t;");
    session.execute("create table t (h int, r int, s int static, c int, PRIMARY KEY((h), r));");
    session.execute("drop table t;");

    // Static column not allowed without range column.
    runInvalidStmt("create table t (h int primary key, s int static, c int);");

    // Primary key column cannot be static.
    runInvalidStmt("create table t (h int static, r int, c int, primary key ((h), r));");
    runInvalidStmt("create table t (h int, r int static, c int, primary key ((h), r));");

    LOG.info("Test End");
  }

  @Test
  public void testSelect() throws Exception {
    LOG.info("Test Start");

    createTable(true);

    // Test select rows with a hash key (1, h1). Static columns should be (13, s13).
    assertQuery("select * from t where h1 = 1 and h2 = 'h1';",
                "Row[1, h1, 1, r1, 13, s13, 1, c1]"+
                "Row[1, h1, 2, r2, 13, s13, 2, c2]"+
                "Row[1, h1, 3, r3, 13, s13, 3, c3]");

    // Test select rows with full primary key (2, h2, 3, r3). Static column should be (23, s23).
    assertQuery("select * from t where h1 = 2 and h2 = 'h2' and r1 = 3 and r2 = 'r3';",
                "Row[2, h2, 3, r3, 23, s23, 3, c3]");

    // Test select distinct static rows with a hash key (3, h3). Static colum should be (33, s33).
    assertQuery("select distinct s1, s2 from t where h1 = 3 and h2 = 'h3';",
                "Row[33, s33]");

    // Test select distinct static rows from the whole table. The order of rows should be stable
    // when the table shards remain the same.
    assertQuery("select distinct h1, h2, s1, s2 from t;",
                "Row[5, h5, 50, s50]"+
                "Row[4, h4, 40, s40]"+
                "Row[1, h1, 13, s13]"+
                "Row[3, h3, 33, s33]"+
                "Row[2, h2, 23, s23]"+
                "Row[6, h6, 60, s60]");

    LOG.info("Test End");
  }

  @Test
  public void testInsert() throws Exception {
    LOG.info("Test Start");

    createTable(false);

    // Insert rows. 3 hash keys with 3 range keys each. The static column is updated with each
    // range key so we should see the same last value across the range values at the end.
    {
      PreparedStatement stmt = session.prepare("insert into t (h1, h2, r1, r2, s1, s2, c1, c2) "+
                                               "values (?, ?, ?, ?, ?, ?, ?, ?);");
      for (int h = 1; h <= 3; h++) {
        for (int r = 1; r <= 3; r++) {
          session.execute(stmt.bind(new Integer(h), "h" + h,
                                    new Integer(r), "r" + r,
                                    new Integer(h*10+r), "s" + (h*10+r), // static columns s1 and s2
                                    new Integer(r), "c" + r // non-static columns c1 and c2
                                    ));
        }
      }
    }

    // Insert 3 other rows with static columns using hash key only.
    {
      PreparedStatement stmt = session.prepare("insert into t (h1, h2, s1, s2) "+
                                               "values (?, ?, ?, ?);");
      for (int h = 4; h <= 6; h++) {
        session.execute(stmt.bind(new Integer(h), "h" + h,
                                  new Integer(h*10), "s" + (h*10) // static columns s1 and s2
                                  ));
      }
    }

    // Test "all" rows. Static columns with hash key only will not show up.
    assertQuery("select * from t;",
                "Row[1, h1, 1, r1, 13, s13, 1, c1]"+
                "Row[1, h1, 2, r2, 13, s13, 2, c2]"+
                "Row[1, h1, 3, r3, 13, s13, 3, c3]"+
                "Row[3, h3, 1, r1, 33, s33, 1, c1]"+
                "Row[3, h3, 2, r2, 33, s33, 2, c2]"+
                "Row[3, h3, 3, r3, 33, s33, 3, c3]"+
                "Row[2, h2, 1, r1, 23, s23, 1, c1]"+
                "Row[2, h2, 2, r2, 23, s23, 2, c2]"+
                "Row[2, h2, 3, r3, 23, s23, 3, c3]");

    // Test select distinct static rows from the whole table. Static columns with hash key only
    // will show up now.
    assertQuery("select distinct h1, h2, s1, s2 from t;",
                "Row[5, h5, 50, s50]"+
                "Row[4, h4, 40, s40]"+
                "Row[1, h1, 13, s13]"+
                "Row[3, h3, 33, s33]"+
                "Row[2, h2, 23, s23]"+
                "Row[6, h6, 60, s60]");

    LOG.info("Test End");
  }

  @Test
  public void testUpdate() throws Exception {
    LOG.info("Test Start");

    createTable(true);

    // Insert rows. 3 hash keys with 3 range keys each. The static column is updated with each
    // range key so we should see the same last value across the range values at the end.
    {
      PreparedStatement stmt = session.prepare("insert into t (h1, h2, r1, r2, s1, s2, c1, c2) "+
                                               "values (?, ?, ?, ?, ?, ?, ?, ?);");
      for (int h = 1; h <= 3; h++) {
        for (int r = 1; r <= 3; r++) {
          session.execute(stmt.bind(new Integer(h), "h" + h,
                                    new Integer(r), "r" + r,
                                    new Integer(h*10+r), "s" + (h*10+r), // static columns s1 and s2
                                    new Integer(r), "c" + r // non-static columns c1 and c2
                                    ));
        }
      }
    }

    // Insert 3 other rows with static columns using hash key only.
    {
      PreparedStatement stmt = session.prepare("insert into t (h1, h2, s1, s2) "+
                                               "values (?, ?, ?, ?);");
      for (int h = 4; h <= 6; h++) {
        session.execute(stmt.bind(new Integer(h), "h" + h,
                                  new Integer(h*10), "s" + (h*10) // static columns s1 and s2
                                  ));
      }
    }

    // Update static and non-static columns for all hash key with range key = [3, r3].
    {
      PreparedStatement stmt = session.prepare("update t set s1 = ?, s2 = ?, c1 = ?, c2 = ? " +
                                               "where h1 = ? and h2 = ? and r1 = 3 and r2 = 'r3';");
      for (int h = 1; h <= 3; h++) {
        session.execute(stmt.bind(new Integer(h*10+5), "s" + (h*10+5), // static columns s1 and s2
                                  new Integer(5), "c" + 5, // non-static columns c1 and c2
                                  new Integer(h), "h" + h));
      }
    }

    // Update static columns for hash-only keys.
    {
      PreparedStatement stmt = session.prepare("update t set s1 = ?, s2 = ? " +
                                               "where h1 = ? and h2 = ?;");
      for (int h = 4; h <= 6; h++) {
        session.execute(stmt.bind(new Integer(h*20), "s" + (h*20), // static columns s1 and s2
                                  new Integer(h), "h" + h));
      }
    }

    // Test "all" rows. Static columns with hash key only will not show up.
    assertQuery("select * from t;",
                "Row[1, h1, 1, r1, 15, s15, 1, c1]"+
                "Row[1, h1, 2, r2, 15, s15, 2, c2]"+
                "Row[1, h1, 3, r3, 15, s15, 5, c5]"+
                "Row[3, h3, 1, r1, 35, s35, 1, c1]"+
                "Row[3, h3, 2, r2, 35, s35, 2, c2]"+
                "Row[3, h3, 3, r3, 35, s35, 5, c5]"+
                "Row[2, h2, 1, r1, 25, s25, 1, c1]"+
                "Row[2, h2, 2, r2, 25, s25, 2, c2]"+
                "Row[2, h2, 3, r3, 25, s25, 5, c5]");

    // Test select distinct static rows from the whole table. Static columns with hash key only
    // will show up now.
    assertQuery("select distinct h1, h2, s1, s2 from t;",
                "Row[5, h5, 100, s100]"+
                "Row[4, h4, 80, s80]"+
                "Row[1, h1, 15, s15]"+
                "Row[3, h3, 35, s35]"+
                "Row[2, h2, 25, s25]"+
                "Row[6, h6, 120, s120]");

    // Update a static columns to null. Expect that row to be gone.
    session.execute("update t set s1 = null, s2 = null where h1 = 6 and h2 = 'h6';");
    assertQuery("select distinct h1, h2, s1, s2 from t;",
                "Row[5, h5, 100, s100]"+
                "Row[4, h4, 80, s80]"+
                "Row[1, h1, 15, s15]"+
                "Row[3, h3, 35, s35]"+
                "Row[2, h2, 25, s25]");

    LOG.info("Test End");
  }

  @Test
  public void testConditionalDml() throws Exception {
    LOG.info("Test Start");

    createTable(true);

    // Insert if not exists of static column.
    {
      PreparedStatement stmt = session.prepare("insert into t (h1, h2, s1, s2) "+
                                               "values (?, ?, ?, ?) if not exists;");
      ResultSet rs1 = session.execute(stmt.bind(new Integer(1), "h1", new Integer(15), "s15"));
      // Expect applied = false because the hash key already exists.
      assertEquals("Row[false]", rs1.one().toString());

      ResultSet rs2 = session.execute(stmt.bind(new Integer(7), "h7", new Integer(75), "s75"));
      // Expect applied = true because the hash key does not exist.
      assertEquals("Row[true]", rs2.one().toString());
    }

    // Insert if not exists of non-static column.
    {
      PreparedStatement stmt = session.prepare("insert into t (h1, h2, r1, r2, s1, s2, c1, c2) "+
                                               "values (?, ?, ?, ?, ?, ?, ?, ?) if not exists;");
      ResultSet rs1 = session.execute(stmt.bind(new Integer(1), "h1",
                                                new Integer(1), "r1",
                                                new Integer(15), "s15",
                                                new Integer(1), "c15"));
      // Expect applied = false because the primary key already exists.
      assertEquals("Row[false]", rs1.one().toString());

      ResultSet rs2 = session.execute(stmt.bind(new Integer(7), "h7",
                                                new Integer(1), "r1",
                                                new Integer(76), "s76",
                                                new Integer(1), "c15"));
      // Expect applied = true because the primary key does not exist.
      assertEquals("Row[true]", rs2.one().toString());
    }

    // Update with static column if-condition.
    {
      PreparedStatement stmt = session.prepare("update t set s1 = ?, s2 = ? " +
                                               "where h1 = ? and h2 = ? " +
                                               "if s1 = ?;");
      ResultSet rs1 = session.execute(stmt.bind(new Integer(16), "s16",
                                                new Integer(1), "h1",
                                                new Integer(15)));
      // Expect applied = false because s1 = 13.
      assertEquals("Row[false, 13]", rs1.one().toString());

      ResultSet rs2 = session.execute(stmt.bind(new Integer(16), "s16",
                                                new Integer(1), "h1",
                                                new Integer(13)));
      // Expect applied = true.
      assertEquals("Row[true]", rs2.one().toString());
    }

    // Update with static and non-static column if-conditions.
    {
      PreparedStatement stmt = session.prepare("update t set s1 = ?, s2 = ? " +
                                               "where h1 = ? and h2 = ? and r1 = ? and r2 = ? " +
                                               "if s1 = ? and c1 = ?;");
      ResultSet rs1 = session.execute(stmt.bind(new Integer(17), "s17",
                                                new Integer(1), "h1",
                                                new Integer(1), "r1",
                                                new Integer(15), new Integer(1)));
      // Expect applied = false because s1 = 16.
      assertEquals("Row[false, 16, 1]", rs1.one().toString());

      ResultSet rs2 = session.execute(stmt.bind(new Integer(17), "s17",
                                                new Integer(1), "h1",
                                                new Integer(1), "r1",
                                                new Integer(16), new Integer(1)));
      // Expect applied = true.
      assertEquals("Row[true]", rs2.one().toString());
    }

    // Test "all" rows. Static columns with hash key only will not show up.
    assertQuery("select * from t where h1 = 1 and h2 = 'h1';",
                "Row[1, h1, 1, r1, 17, s17, 1, c1]"+
                "Row[1, h1, 2, r2, 17, s17, 2, c2]"+
                "Row[1, h1, 3, r3, 17, s17, 3, c3]");

    // Test select distinct static rows from the whole table. Static columns with hash key only
    // will show up now.
    assertQuery("select distinct h1, h2, s1, s2 from t;",
                "Row[7, h7, 76, s76]"+
                "Row[5, h5, 50, s50]"+
                "Row[4, h4, 40, s40]"+
                "Row[1, h1, 17, s17]"+
                "Row[3, h3, 33, s33]"+
                "Row[2, h2, 23, s23]"+
                "Row[6, h6, 60, s60]");

    LOG.info("Test End");
  }

  @Test
  public void testCollection() throws Exception {
    LOG.info("Test Start");

    session.execute("create table t (" +
                    "h1 int, h2 varchar, " +
                    "r1 int, r2 varchar, " +
                    "s1 int static, s2 set<varchar> static , s3 map<varchar, int> static, " +
                    "c1 int, c2 varchar, " +
                    "primary key ((h1, h2), r1, r2));");

    // Test select rows with a hash key (1, h1). Expect updated s1 and c1.
    session.execute("insert into t (h1, h2, r1, r2, s1, s2, s3, c1, c2) " +
                    "values (1, 'h1', 1, 'r1', 1, {'a', 'b'}, {'a' : 1, 'b' : 1}, 1, 'c1');");
    session.execute("insert into t (h1, h2, r1, r2, s1, s2, s3, c1, c2) " +
                    "values (1, 'h1', 2, 'r2', 2, {'c', 'd'}, {'c' : 1, 'd' : 1}, 2, 'c2');");
    session.execute("insert into t (h1, h2, s1, s2, s3) " +
                    "values (2, 'h2', 3, {'e', 'f'}, {'e' : 1, 'f' : 1});");

    // Verify the static collection columns
    assertQuery("select * from t;",
                "Row[1, h1, 1, r1, 2, [c, d], {c=1, d=1}, 1, c1]"+
                "Row[1, h1, 2, r2, 2, [c, d], {c=1, d=1}, 2, c2]");
    assertQuery("select distinct h1, h2, s1, s2, s3 from t;",
                "Row[1, h1, 2, [c, d], {c=1, d=1}]"+
                "Row[2, h2, 3, [e, f], {e=1, f=1}]");

    LOG.info("Test End");
  }

  @Test
  public void testTTL() throws Exception {
    LOG.info("Test Start");

    createTable(true);

    // Update static and non-static column with TTL.
    session.execute("update t using ttl 5 set s1 = 14, c1 = 4 " +
                    "where h1 = 1 and h2 = 'h1' and r1 = 1 and r2 = 'r1';");

    // Test select rows with a hash key (1, h1). Expect updated s1 and c1.
    assertQuery("select * from t where h1 = 1 and h2 = 'h1';",
                "Row[1, h1, 1, r1, 14, s13, 4, c1]"+
                "Row[1, h1, 2, r2, 14, s13, 2, c2]"+
                "Row[1, h1, 3, r3, 14, s13, 3, c3]");

    Thread.sleep(5000);

    // Test select rows with a hash key (1, h1) again. Expect s1 and c1 gone.
    assertQuery("select * from t where h1 = 1 and h2 = 'h1';",
                "Row[1, h1, 1, r1, NULL, s13, NULL, c1]"+
                "Row[1, h1, 2, r2, NULL, s13, 2, c2]"+
                "Row[1, h1, 3, r3, NULL, s13, 3, c3]");

    // Update static and non-static column with TTL again.
    session.execute("update t using ttl 5 set s1 = 14, c1 = 4 " +
                    "where h1 = 1 and h2 = 'h1' and r1 = 1 and r2 = 'r1';");
    // Update static column alone with another TTL again.
    session.execute("update t using ttl 10 set s1 = 15 " +
                    "where h1 = 1 and h2 = 'h1';");

    // Test select rows with a hash key (1, h1). Expect updated s1 and c1.
    assertQuery("select * from t where h1 = 1 and h2 = 'h1';",
                "Row[1, h1, 1, r1, 15, s13, 4, c1]"+
                "Row[1, h1, 2, r2, 15, s13, 2, c2]"+
                "Row[1, h1, 3, r3, 15, s13, 3, c3]");

    Thread.sleep(5000);

    // Test select rows with a hash key (1, h1) again. Expect c1 gone but s1 hasn't.
    assertQuery("select * from t where h1 = 1 and h2 = 'h1';",
                "Row[1, h1, 1, r1, 15, s13, NULL, c1]"+
                "Row[1, h1, 2, r2, 15, s13, 2, c2]"+
                "Row[1, h1, 3, r3, 15, s13, 3, c3]");

    Thread.sleep(5000);

    // Test select rows with a hash key (1, h1) again. Expect s1 gone also.
    assertQuery("select * from t where h1 = 1 and h2 = 'h1';",
                "Row[1, h1, 1, r1, NULL, s13, NULL, c1]"+
                "Row[1, h1, 2, r2, NULL, s13, 2, c2]"+
                "Row[1, h1, 3, r3, NULL, s13, 3, c3]");

    // Insert static column alone with TTL.
    session.execute("insert into t (h1, h2, s1, s2) values (7, 'h7', 17, 's17') using ttl 5;");

    // Verify the static column.
    assertQuery("select distinct h1, h2, s1, s2 from t where h1 = 7 and h2 = 'h7';",
                "Row[7, h7, 17, s17]");

    Thread.sleep(5000);

    // Verify the static column is gone.
    assertQuery("select distinct h1, h2, s1, s2 from t where h1 = 7 and h2 = 'h7';",
                "");

    LOG.info("Test End");
  }

  @Test
  public void testDeleteStaticColumn() throws Exception {
  	LOG.info("Test Start");
    session.execute("create table t (" +
                    "h int, r int, v int static," +
                    "primary key (h, r));");

    // Test select rows with a hash key (1, h1). Expect updated s1 and c1.
    session.execute("insert into t (h, r, v) " +
                    "values (1, 1, 1);");
    session.execute("insert into t (h, r, v) " +
                    "values (1, 2, 2);");

    // Verify the static collection columns
    String delete_stmt = "delete v from t where h = 1;";
    session.execute(delete_stmt);
    String select_stmt = "select v from t;";
    ResultSet rs = session.execute(select_stmt);
    List<Row> rows = rs.all();
    assertEquals(2, rows.size());
    for (Row row : rows) {
    		assertTrue(row.isNull(0));
    }
    LOG.info("Test End");
  }
}
