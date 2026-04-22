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

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressPgAsync extends BasePgRegressTestPorted {

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    BasePgListenNotifyTest.addListenNotifyFlags(flagMap);
    flagMap.put("cdc_max_virtual_wal_per_tserver", "10");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    BasePgListenNotifyTest.addListenNotifyFlags(flagMap);
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    BasePgListenNotifyTest.waitForNotificationsTableReady(connection, getConnectionBuilder());
    runPgRegressTest("yb_pg_async_schedule");
  }

  @Test
  public void testIsolationPgRegress() throws Exception {
    // The isolation tester caches backend PIDs via PQbackendPID() and matches
    // them against notification sender PIDs. With connection manager, the
    // actual backend can change, causing PID mismatches.
    skipYsqlConnMgr(UNIQUE_PHYSICAL_CONNS_NEEDED);
    BasePgListenNotifyTest.waitForNotificationsTableReady(connection, getConnectionBuilder());
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
