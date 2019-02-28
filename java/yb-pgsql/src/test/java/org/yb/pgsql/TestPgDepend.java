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
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgDepend extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgDepend.class);

  @Test
  public void testPgDependInsertion() throws SQLException {
    createSimpleTable("test");
    try (Statement statement = connection.createStatement()) {
      // Create an Index for the table.
      statement.execute("CREATE UNIQUE INDEX test_h on test(h)");

      // Get the OID of the table.
      ResultSet rs = statement.executeQuery("SELECT oid FROM pg_class WHERE relname = 'test'");
      rs.next();
      int oidTable = rs.getInt("oid");

      // Get the OID of the index.
      rs = statement.executeQuery("SELECT oid FROM pg_class WHERE relname = 'test_h'");
      rs.next();
      int oidIndex = rs.getInt("oid");

      // Check that we have inserted the dependency into pg_depend.
      rs = statement.executeQuery("SELECT * FROM pg_depend "
                                  + "WHERE objid=" + oidIndex + "AND refobjid=" + oidTable);
      int rs_count = 0;
      while(rs.next()) ++rs_count;
      assertEquals(rs_count, 1);
    }
  }
  public void testPgDependDeletion() throws SQLException {
    createSimpleTable("test");
    try (Statement statement = connection.createStatement()) {
      // Create an Index for the table.
      statement.execute("CREATE INDEX test_h on test(h)");

      // Get the OID of the table.
      ResultSet rs = statement.executeQuery("SELECT oid FROM pg_class WHERE relname = 'test'");
      rs.next();
      int oidTable = rs.getInt("oid");

      // Get the OID of the index.
      rs = statement.executeQuery("SELECT oid FROM pg_class WHERE relname = 'test_h'");
      rs.next();
      int oidIndex = rs.getInt("oid");

      // Check that we have inserted into pg_index.
      rs = statement.executeQuery("SELECT * FROM pg_index "
                                  + "WHERE indexrelid=" + oidIndex + "AND indrelid=" + oidTable);
      int rs_count = 0;
      while(rs.next()) ++rs_count;
      assertEquals(rs_count, 1);

      statement.execute("DROP TABLE test");

      // Check that we have deleted the dependency in pg_depend.
      rs = statement.executeQuery("SELECT * FROM pg_depend "
                                  + "WHERE objid=" + oidIndex + "and refobjid=" + oidTable);
      rs_count = 0;
      while(rs.next()) ++rs_count;
      assertEquals(rs_count, 0);

      // Check that we have deleted the index's entry in pg_index.
      rs = statement.executeQuery("SELECT * FROM pg_index "
                                  + "WHERE indexrelid=" + oidIndex + "AND indrelid=" + oidTable);
      rs_count = 0;
      while(rs.next()) ++rs_count;
      assertEquals(rs_count, 0);

      // Check that we have deleted the index's entry in pg_class.
      rs = statement.executeQuery("SELECT * FROM pg_class WHERE relname = 'test_h'");
      rs_count = 0;
      while(rs.next()) ++rs_count;
      assertEquals(rs_count, 0);

      // Check that we can create a new index with the same name.
      createSimpleTable("test");
      statement.execute("CREATE INDEX test_h on test(h)");
    }
  }
}
