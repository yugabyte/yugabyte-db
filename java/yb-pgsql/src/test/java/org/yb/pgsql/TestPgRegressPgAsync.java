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

import java.util.Map;
import java.sql.Connection;
import java.sql.Statement;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.util.Timeouts;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressPgAsync extends BasePgRegressTestPorted {

  private void waitForObect(Connection conn, String stmt) throws Exception {
    Statement statement = conn.createStatement();
    TestUtils.waitFor(() -> {
      Row row = getSingleRow(statement, stmt);
      return row.getInt(0) == 1;
    }, Timeouts.adjustTimeoutSecForBuildType(120 * 1000));
  }

  // Wait for master to create yb_system database.
  private void waitForYbSystemDB() throws Exception {
    waitForObect(connection,
        "SELECT" +
            "  CASE" +
            "      WHEN EXISTS (" +
            "          SELECT 1 FROM pg_database WHERE datname = 'yb_system'" +
            "      )" +
            "      THEN 1" +
            "      ELSE 0" +
            "  END");
  }

  // Wait for master to create pg_yb_notifications table.
  private void waitForNotificationsTable() throws Exception {
    waitForYbSystemDB();
    Connection conn = getConnectionBuilder().withDatabase("yb_system").connect();
    waitForObect(conn,
        " SELECT" +
            "    CASE" +
            "        WHEN EXISTS (" +
            "            SELECT 1 FROM pg_class" +
            "            WHERE relname = 'pg_yb_notifications'" +
            "              AND relkind = 'r'" +
            "              AND relnamespace = 2200" +
            "        )" +
            "        THEN 1" +
            "        ELSE 0" +
            "    END ");
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("TEST_ysql_yb_enable_listen_notify", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("TEST_ysql_yb_enable_listen_notify", "true");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    waitForNotificationsTable();
    runPgRegressTest("yb_pg_async_schedule");
  }

  @Test
  public void testIsolationPgRegress() throws Exception {
    waitForNotificationsTable();
    runPgRegressTest(
      PgRegressBuilder.PG_ISOLATION_REGRESS_DIR /* inputDir */,
      "yb_pg_async_isolation_schedule",
      0 /* maxRuntimeMillis */, PgRegressBuilder.PG_ISOLATION_REGRESS_EXECUTABLE);
  }

  @Override
  protected Map<String, String> getPgRegressEnvVars() {
    Map<String, String> envs = super.getPgRegressEnvVars();

    // In YB, it takes longer to deliver the notifications. In order to match the
    // PG's expected output for isolation test async-notify, introduce a sleep in
    // isolationtester.c after completing execution of each step and before
    // checking for notifications.
    envs.put("YB_ISOLATION_TEST_WAIT_FOR_NOTIFS_MS", "500");
    return envs;
  }
}
