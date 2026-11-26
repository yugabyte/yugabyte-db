// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;

import java.sql.Connection;
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestTempTableStickiness extends BaseYsqlConnMgr {

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(SINGLE_BACKEND_FLAGS);
  }

  private int stickyObjectCount(Statement stmt) throws Exception {
    return queryInt(stmt, "SHOW ysql_conn_mgr_sticky_object_count");
  }

  private void assertStickyUntilDisconnect(String sql) throws Exception {
    QueuedClient client;
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute(sql);
      assertNotEquals(0, stickyObjectCount(stmt));
      client = startQueuedClient();
      client.assertBlocked();
    }
    client.assertServed();
  }

  // CALL a procedure that creates a temp table and commits: the second client stays blocked until
  // the first client disconnects.
  @Test
  public void testProcedureCommit() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE PROCEDURE create_temp() LANGUAGE plpgsql AS $$ " +
          "BEGIN CREATE TEMP TABLE t(a int); COMMIT; END $$");
    }
    assertStickyUntilDisconnect("CALL create_temp()");
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("DROP PROCEDURE create_temp");
    }
  }

  // Create a temp table in a Query message that commits and runs another statement before its
  // single ReadyForQuery: the second client stays blocked until the first client disconnects.
  @Test
  public void testQueryAcrossCommit() throws Exception {
    assertStickyUntilDisconnect("BEGIN; CREATE TEMP TABLE t(a int); COMMIT; SELECT 1");
  }

  // As above, with the temp table created in a released savepoint.
  @Test
  public void testReleasedSavepointAcrossCommit() throws Exception {
    assertStickyUntilDisconnect(
        "BEGIN; SAVEPOINT s; CREATE TEMP TABLE t(a int); RELEASE s; COMMIT; SELECT 1");
  }

  // As above, with the temp table created in a savepoint that is rolled back: the second client
  // is served.
  @Test
  public void testRolledBackSavepointAcrossCommit() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute(
          "BEGIN; SAVEPOINT s; CREATE TEMP TABLE t(a int); ROLLBACK TO s; COMMIT; SELECT 1");
      assertEquals(0, stickyObjectCount(stmt));
      assertQueuedClientServed();
    }
  }
}
