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

import java.util.*;

import org.junit.Test;
import org.junit.runner.RunWith;
import com.yugabyte.util.PSQLException;
import com.yugabyte.util.PSQLWarning;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Connection;
import java.sql.Statement;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgMisc extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestPgMisc.class);

  @Override
  protected void resetSettings() {
    super.resetSettings();
    // Starts CQL proxy for the cross Postgres/CQL testNamespaceSeparation test case.
    startCqlProxy = true;
  }

  @Test
  public void testTableCreationInTemplate() throws Exception {
    try {
      executeQueryInTemplate("CREATE TABLE test (a int);");
      fail("Table created in template");
    } catch(PSQLException e) {
    }
  }

  @Test
  public void testTableCreationAsInTemplate() throws Exception {
    try {
      executeQueryInTemplate("CREATE TABLE test AS SELECT 1;");
      fail("Table created in template");
    } catch(PSQLException e) {
    }
  }

  @Test
  public void testSequenceCreationInTemplate() throws Exception {
    try {
      executeQueryInTemplate("CREATE SEQUENCE test START 101;");
      fail("Sequence created in template");
    } catch(PSQLException e) {
    }
  }

  @Test
  public void testVacuum() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("VACUUM;");
      if (statement.getWarnings() != null) {
        throw statement.getWarnings();
      }
      fail("Vacuum executed without warnings");
    } catch(PSQLWarning w) {
    }
  }

  @Test
  public void testTemporaryTableAnalyze() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TEMP TABLE test_table(a int);");
      statement.execute("ANALYZE test_table;");
      if (statement.getWarnings() != null) {
        throw statement.getWarnings();
      }
    } catch(PSQLException e) {
      fail("Analyze executed with exception");
    } catch(PSQLWarning w) {
      fail("Analyze executed with warning");
    }
  }

  /*
   * Test for CHECKPOINT no-op functionality
   */
  @Test
  public void testCheckpoint() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CHECKPOINT");
      if (statement.getWarnings() != null) {
        throw statement.getWarnings();
      }
      fail("Checkpoint executed without warnings");
    } catch(PSQLWarning w) {
    }
  }

  @Test
  public void testTemporaryTableTransactionInExecute() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TEMP TABLE test_table(a int, b int, c int, PRIMARY KEY (a))");

      // Can insert using JDBC prepared statement.
      statement.execute("INSERT INTO test_table(a, b, c) VALUES (1, 2, 3)");

      // Can insert explicitly prepared statement.
      statement.execute("PREPARE ins AS INSERT INTO test_table(a, b, c) VALUES (2, 3, 4)");
      statement.execute("EXECUTE ins");
    }
  }

  @Test
  public void testTemporaryTableTransactionInProcedure() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TEMP TABLE test_table(k int PRIMARY KEY)");
      stmt.execute("CREATE PROCEDURE test_temp_table(k int) AS $$ " +
        "BEGIN" +
        "  INSERT INTO test_table VALUES(k);" +
        "END; $$ LANGUAGE 'plpgsql';");
      stmt.execute("CALL test_temp_table(1)");
      stmt.execute("CALL test_temp_table(2)");
      // Can insert explicitly prepared statement.
      assertOneRow(stmt, "SELECT COUNT(*) FROM test_table", 2);
    }
  }

  private void executeQueryInTemplate(String query) throws Exception {
    try (Connection connection = getConnectionBuilder().withTServer(0).withDatabase("template1")
        .connect();
        Statement statement = connection.createStatement()) {
      statement.execute(query);
    }
  }
}
