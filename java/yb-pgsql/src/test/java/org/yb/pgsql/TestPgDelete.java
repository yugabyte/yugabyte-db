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
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import org.junit.runner.RunWith;

import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgDelete extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgDelete.class);

  @Test
  public void testBasicDelete() throws SQLException {
    Set<Row> allRows = setupSimpleTable("test_delete");

    try (Statement statement = connection.createStatement()) {
      String query = "SELECT h FROM test_delete WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(10, rcount);
      }
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("DELETE FROM test_delete WHERE h = 2");
    }

    try (Statement statement = connection.createStatement()) {
      String query = "SELECT h FROM test_delete WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        int rcount = 0;
        while (rs.next()) rcount++;
        assertEquals(0, rcount);
      }
    }
  }
}
