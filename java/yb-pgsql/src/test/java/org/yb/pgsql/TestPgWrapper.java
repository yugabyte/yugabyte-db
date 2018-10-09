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

import org.apache.commons.lang3.RandomUtils;
import org.junit.Test;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

import org.postgresql.core.TransactionState;
import org.postgresql.util.PSQLException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import org.junit.runner.RunWith;

import org.yb.YBTestRunner;
import org.yb.client.TestUtils;

@RunWith(value=YBTestRunner.class)
public class TestPgWrapper extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgWrapper.class);

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

    createSimpleTable("test", "v");

    // Test table with out of order primary key columns.
    statement.execute("CREATE TABLE test2(v text, r float, h bigint, PRIMARY KEY (h, r))");

    // Table already exists.
    runInvalidQuery(statement, getSimpleTableCreationStatement("test", "v"));

    // Test drop table.
    statement.execute("DROP TABLE test");
    statement.execute("DROP TABLE test2");

    // Table does not exist.
    runInvalidQuery(statement, "DROP TABLE test");

    statement.close();
  }

  @Test
  public void testDatatypes() throws SQLException {
    LOG.info("START testDatatypes");
    String[] supported_types = {"smallint", "int", "bigint", "real", "double precision", "text"};

    Statement statement = connection.createStatement();

    StringBuilder sb = new StringBuilder();
    sb.append("CREATE TABLE test(");
    // Checking every type is allowed for a column.
    for (int i = 0; i < supported_types.length; i++) {
      sb.append("c").append(i).append(" ");
      sb.append(supported_types[i]);
      sb.append(", ");
    }

    sb.append("PRIMARY KEY(");
    // Checking every type is allowed as primary key.
    for (int i = 0; i < supported_types.length; i++) {
      if (i > 0) sb.append(", ");
      sb.append("c").append(i);
    }
    sb.append("))");

    String sql = sb.toString();
    LOG.info("Creating table. SQL statement: " + sql);
    statement.execute(sb.toString());
    LOG.info("END testDatatypes");
  }

  @Test
  public void testSimpleDML() throws Exception {
    Statement statement = connection.createStatement();
    statement.execute("CREATE TABLE test(h bigint, r float, v text, PRIMARY KEY (h, r))");

    statement.execute("INSERT INTO test(h, r, v) VALUES (1, 2.5, 'abc')");
    statement.execute("INSERT INTO test(h, r, v) VALUES (1, 3.5, 'def')");

    ResultSet rs = statement.executeQuery("SELECT h, r, v FROM test WHERE h = 1");

    assertTrue(rs.next());
    assertEquals(1, rs.getLong("h"));
    assertEquals(2.5, rs.getDouble("r"));
    assertEquals("abc", rs.getString("v"));

    assertTrue(rs.next());
    assertEquals(1, rs.getLong("h"));
    assertEquals(3.5, rs.getDouble("r"));
    assertEquals("def", rs.getString("v"));

    assertFalse(rs.next());

    rs.close();
    statement.close();
  }

}
