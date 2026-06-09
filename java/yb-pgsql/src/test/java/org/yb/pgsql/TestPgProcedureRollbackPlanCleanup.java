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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import java.sql.PreparedStatement;
import java.sql.Statement;

/**
 * Regression test for a segfault in GetActualStmtNode (pg_yb_utils.c) caused
 * by ROLLBACK inside a procedure cleaning up the outermost portal's cached
 * plan while YB ProcessUtility hooks still reference the freed PlannedStmt.
 *
 * The crash requires:
 *   1. Extended query protocol (JDBC PreparedStatement)
 *   2. Procedure body that executes ROLLBACK
 * The ROLLBACK triggers AtAbort_Portals which calls PortalReleaseCachedPlan on
 * the active outermost portal. For a custom plan (refcount 1) the underlying
 * CachedPlan is freed, but the PlannedStmt pointer already handed to the
 * ProcessUtility hook chain is now dangling.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgProcedureRollbackPlanCleanup extends BasePgSQLTest {

  @Test
  public void testParameterizedCallWithRollback() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE test_rb(id INT PRIMARY KEY, val TEXT)");
      stmt.execute(
          "CREATE PROCEDURE proc_rb(n INT) LANGUAGE plpgsql AS $$ " +
          "BEGIN " +
          "  INSERT INTO test_rb VALUES (n, 'v'); " +
          "  ROLLBACK; " +
          "  UPDATE test_rb SET val = 'a' WHERE id = n; " +
          "  COMMIT; " +
          "END; $$");
    }

    try (PreparedStatement pstmt = connection.prepareStatement("CALL proc_rb(?)")) {
      pstmt.setInt(1, 1);
      pstmt.execute();
    }
  }
}
