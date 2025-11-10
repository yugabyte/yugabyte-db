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
import org.yb.util.BuildTypeUtil;

/**
 * Runs the pg_regress authorization-related tests on YB code.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgRegressLock extends BasePgRegressTestPorted {

  private static final int DELAY_FORWARDING_WAITING_PROBES_MS =
      (int) (500 * BuildTypeUtil.getTimeoutMultiplier());

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_object_locking_for_table_locks", "true");
    // TODO(#27819): Revert to default waiter timeout once the GH is addressed.
    flagMap.put("refresh_waiter_timeout_ms", "5000");
    flagMap.put("ysql_yb_ddl_transaction_block_enabled", "true");
    flagMap.put("TEST_delay_forwarding_waiting_probes_ms",
                String.valueOf(DELAY_FORWARDING_WAITING_PROBES_MS));
    appendToYsqlPgConf(flagMap, "statement_timeout=300000");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest("yb_pg_lock_schedule");
  }

  @Test
  public void testIsolation() throws Exception {
    runPgRegressTest(
        PgRegressBuilder.PG_ISOLATION_REGRESS_DIR /* inputDir */,
        "yb_pg_isolation_lock_schedule",
        0 /* maxRuntimeMillis */, PgRegressBuilder.PG_ISOLATION_REGRESS_EXECUTABLE);
  }
}
