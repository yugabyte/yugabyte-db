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
import org.yb.YBTestRunner;

import java.util.Map;

@RunWith(value=YBTestRunner.class)
public class TestPgWithoutWaitQueuesIsolationRegress extends BasePgSQLTest {

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_wait_queues", "false");
    flagMap.put("enable_deadlock_detection", "false");
    flagMap.put("ysql_sleep_before_retry_on_txn_conflict", "true");
    flagMap.put("yb_enable_read_committed_isolation", "true");
    flagMap.put("ysql_max_write_restart_attempts", "20");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 180;
  }

  @Test
  public void runPgTransparentRestartsRegressTest() throws Exception {
    runPgRegressTest(
      PgRegressBuilder.PG_ISOLATION_REGRESS_DIR, "yb_without_wait_queues_schedule",
      0 /* maxRuntimeMillis */, PgRegressBuilder.PG_ISOLATION_REGRESS_EXECUTABLE);
  }
}
