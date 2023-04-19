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
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;


@RunWith(value=YBTestRunner.class)
public class TestPgDomain extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgDomain.class);

  @Test
  public void testCreateDrop() throws SQLException {
    try (Statement statement = connection.createStatement()) {
      List<Row> expectedRows = new ArrayList<>();

      statement.execute("CREATE DOMAIN keys VARCHAR(10) NOT NULL");
      statement.execute("CREATE DOMAIN idx INT CHECK (VALUE > 0) DEFAULT 10");
      statement.execute("CREATE TABLE t (h keys, v idx)");

      statement.execute("INSERT INTO t (h, v) VALUES ('k_0', 1)");
      expectedRows.add(new Row("k_0", 1));
      statement.execute("INSERT INTO t (h) VALUES ('k_1')");
      expectedRows.add(new Row("k_1", 10));

      ResultSet rs = statement.executeQuery("SELECT * FROM t");
      assertEquals(expectedRows, getSortedRowList(rs));

      // Cannot create another domain with same name.
      runInvalidQuery(statement, "CREATE DOMAIN idx INT CHECK (VALUE < 0)", "already exists");
      // Cannot break domain constraint.
      runInvalidQuery(statement, "INSERT INTO t (h, v) VALUES ('k_2', 0)",
         "violates check constraint");
      runInvalidQuery(statement, "INSERT INTO t (v) VALUES (1)",
              "does not allow null values");
      // Cannot drop an domain in use.
      runInvalidQuery(statement, "DROP DOMAIN idx", "depends on type");
      // Cannot drop a domain that doesn't exist.
      runInvalidQuery(statement, "DROP DOMAIN idx_no_exist", "does not exist");

      statement.execute("ALTER DOMAIN idx DROP DEFAULT");
      statement.execute("INSERT INTO t (h) VALUES ('k_2')");
      expectedRows.add(new Row("k_2", null));

      rs = statement.executeQuery("SELECT * FROM t");
      assertEquals(expectedRows, getSortedRowList(rs));

      statement.execute("ALTER DOMAIN idx SET DEFAULT 5");
      statement.execute("INSERT INTO t (h) VALUES ('k_3')");
      expectedRows.add(new Row("k_3", 5));

      rs = statement.executeQuery("SELECT * FROM t");
      assertEquals(expectedRows, getSortedRowList(rs));

      statement.execute("ALTER DOMAIN idx RENAME TO idx_new");
      runInvalidQuery(statement, "DROP DOMAIN idx", "does not exist");
      runInvalidQuery(statement, "DROP DOMAIN idx_new", "depends on type");

      statement.execute("DROP TABLE t");
      statement.execute("DROP DOMAIN idx_new");

      // Cannot create a table on a domain that doesn't exist.
      runInvalidQuery(statement, "CREATE TABLE t (h idx_new, v text)", "does not exist");
    }
  }
}
