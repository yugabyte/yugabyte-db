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
package org.yb.multiapi;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import java.sql.Statement;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.exceptions.QueryValidationException;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestMultiAPI extends TestMultiAPIBase {
  private static final Logger LOG = LoggerFactory.getLogger(TestMultiAPI.class);

  // This tests the fix for issue https://github.com/YugaByte/yugabyte-db/issues/2665.
  // Testing the case when there is CQL keyspace with the same name as a SQL DB.
  @Test
  public void testDropDB() throws Exception {
    String dbName = "dtest";
    LOG.info("Start test: " + getCurrentTestMethodName());

    try (Statement statement = connection.createStatement()) {
      LOG.info("Create SQL DB");
      statement.execute(String.format("CREATE DATABASE %s", dbName));

      LOG.info("Create CQL keyspace with used DB name");
      cqlSession.execute(String.format("CREATE KEYSPACE %s", dbName));

      LOG.info("Drop SQL DB");
      statement.execute(String.format("DROP DATABASE %s", dbName));

      LOG.info("Drop CQL keyspace");
      cqlSession.execute(String.format("DROP KEYSPACE %s", dbName));
    }
    LOG.info("End test: " + getCurrentTestMethodName());
  }

  protected void assertResult(ResultSet rs, Set<String> expectedRows) {
    Set<String> actualRows = new HashSet<>();
    for (com.datastax.driver.core.Row row : rs) {
      actualRows.add(row.toString());
    }
    assertEquals(expectedRows, actualRows);
  }

  @Test
  public void testNamespaceSeparation() throws Exception {
    // Verify that namespaces for YSQL databases are not shown in YCQL.
    assertResult(cqlSession.execute("select keyspace_name from system_schema.keyspaces;"),
                 new HashSet<String>(Arrays.asList("Row[cql_test_keyspace]",
                                                   "Row[system]",
                                                   "Row[system_auth]",
                                                   "Row[system_schema]")));

    // Verify that YSQL table cannot be created in namespaces for YSQL databases.
    try {
      cqlSession.execute("create table template1.t (a int primary key);");
      fail("YCQL table created in namespace for YSQL database");
    } catch (QueryValidationException e) {
      LOG.info("Expected exception", e);
    }

    // Verify that YSQL database can be created with the same name as an existing YCQL keyspace.
    connection.createStatement().execute("create database system;");
  }

}
