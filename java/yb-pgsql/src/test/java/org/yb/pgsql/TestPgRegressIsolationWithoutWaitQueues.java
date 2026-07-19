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

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressIsolationWithoutWaitQueues extends BasePgRegressTest {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_wait_queues", "false");
    flagMap.put("yb_enable_read_committed_isolation", "true");
    flagMap.put("skip_prefix_locks", "false");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 800;
  }

  private void runIsolationRegressTest(String schedule) throws Exception {
    runPgRegressTest(
      PgRegressBuilder.PG_ISOLATION_REGRESS_DIR, schedule,
      0 /* maxRuntimeMillis */, PgRegressBuilder.PG_ISOLATION_REGRESS_EXECUTABLE);
  }

  @Test
  // The isolation tester interleaves statements from concurrent sessions inside open
  // transactions and relies on each session keeping its own dedicated backend. With Connection
  // Manager a backend serving one session can be reused for another session's statement, so a
  // conflicting UPDATE fails to observe the first session's uncommitted row lock. This surfaces
  // as a session no longer seeing its own committed write and a spurious "transaction aborted,
  // data already sent" error in the READ COMMITTED check-constraint-locking permutation. Run on
  // the Postgres port instead, like TestPgRegressIsolationWithTxnDdl and TestPgRegressLock.
  @BypassConnMgr(reason = BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED)
  public void testPgRegressWithoutSkipPrefixLocks() throws Exception {
    runIsolationRegressTest("yb_without_wait_queues_schedule_skip_prefix_locks_off");
  }

  @Test
  public void testPgRegressWithSkipPrefixLocks() throws Exception {
    Map<String, String> flagMap = new HashMap<>();
    flagMap.put("skip_prefix_locks", "true");
    flagMap.put("ysql_enable_packed_row", "true");
    restartClusterWithFlags(Collections.emptyMap(), flagMap);
    runIsolationRegressTest("yb_without_wait_queues_schedule");
  }
}
