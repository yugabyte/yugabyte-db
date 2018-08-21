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

import org.junit.Test;

import java.sql.ResultSet;
import java.sql.Statement;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.runner.RunWith;

import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgWrapper extends BasePgSQLTest {

  @Test
  public void testSimpleDDL() throws Exception {
    Statement statement = connection.createStatement();

    // ---------------------------------------------------------------------------------------------
    // Test Database

    statement.execute("CREATE DATABASE dbtest");

    // Database already exists.
    runInvalidQuery(statement, "CREATE DATABASE dbtest");

    statement.execute("DROP DATABASE dbtest");

    // Database does not exist.
    runInvalidQuery(statement, "DROP DATABASE dbtest");

    // ---------------------------------------------------------------------------------------------
    // Test Table

    statement.execute("CREATE TABLE test(h int, r int, v int, PRIMARY KEY (h, r))");

    // Table already exists.
    runInvalidQuery(statement, "CREATE TABLE test(h int, r int, v int, PRIMARY KEY (h, r))");

    statement.execute("DROP TABLE test");

    // Database does not exist.
    runInvalidQuery(statement, "DROP TABLE test");

    statement.close();
  }


  @Test
  public void testSimpleDML() throws Exception {
    Statement statement = connection.createStatement();
    statement.execute("CREATE TABLE test(h int, r int, v int, PRIMARY KEY (h, r))");

    statement.execute("INSERT INTO test(h, r, v) VALUES (1, 2, 3)");
    statement.execute("INSERT INTO test(h, r, v) VALUES (1, 3, 4)");

    ResultSet rs = statement.executeQuery("SELECT h, r, v FROM test WHERE h = 1;");

    assertTrue(rs.next());
    assertEquals(1, rs.getInt("h"));
    assertEquals(2, rs.getInt("r"));
    assertEquals(3, rs.getInt("v"));

    assertTrue(rs.next());
    assertEquals(1, rs.getInt("h"));
    assertEquals(3, rs.getInt("r"));
    assertEquals(4, rs.getInt("v"));

    assertFalse(rs.next());

    rs.close();
    statement.close();
  }

  @Test
  public void testJoins() throws Exception {
    Statement statement = connection.createStatement();
    statement.execute("CREATE TABLE t1(h int, r int, v int, PRIMARY KEY (h, r))");

    statement.execute("INSERT INTO t1(h, r, v) VALUES (1, 2, 3)");
    statement.execute("INSERT INTO t1(h, r, v) VALUES (1, 3, 4)");
    statement.execute("INSERT INTO t1(h, r, v) VALUES (1, 4, 5)");

    statement.execute("CREATE TABLE t2(h int, r int, v2 int, PRIMARY KEY (h, r))");

    statement.execute("INSERT INTO t2(h, r, v2) VALUES (1, 2, 4)");
    statement.execute("INSERT INTO t2(h, r, v2) VALUES (1, 4, 6)");

    ResultSet rs = statement.executeQuery("SELECT a.h, a.r, a.v, b.v2 FROM " +
                                              "t1 a LEFT JOIN t2 b " +
                                              "ON (a.h = b.h and a.r = b.r)" +
                                              "WHERE a.h = 1 AND a.r IN (2,3);");

    assertTrue(rs.next());
    assertEquals(1, rs.getInt("h"));
    assertEquals(2, rs.getInt("r"));
    assertEquals(3, rs.getInt("v"));
    assertEquals(4, rs.getInt("v2"));

    assertTrue(rs.next());
    assertEquals(1, rs.getInt("h"));
    assertEquals(3, rs.getInt("r"));
    assertEquals(4, rs.getInt("v"));
    assertEquals(0, rs.getInt("v2"));
    assertTrue(rs.wasNull());

    assertFalse(rs.next());

    rs.close();
    statement.close();
  }

  @Test
  public void testBuiltins() throws Exception {
    Statement statement = connection.createStatement();
    statement.execute("CREATE TABLE test(h int, r int, v int, PRIMARY KEY (h, r))");

    statement.execute("INSERT INTO test(h,r,v) VALUES (floor(1 + 1.5), log(3, 27), ceil(pi()))");

    ResultSet rs = statement.executeQuery("SELECT h, r, v FROM test WHERE h = 2;");

    assertTrue(rs.next());
    assertEquals(2, rs.getInt("h"));
    assertEquals(3, rs.getInt("r"));
    assertEquals(4, rs.getInt("v"));

    assertFalse(rs.next());

    rs.close();
    statement.close();
  }
}
