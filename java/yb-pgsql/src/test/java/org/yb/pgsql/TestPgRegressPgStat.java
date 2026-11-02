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
public class TestPgRegressPgStat extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // yb.orig.pg_stat_backend_1.out lists idle pooled backends left by
    // yb.orig.pg_stat, so conn mgr must not expire them during the schedule.
    flagMap.put("ysql_conn_mgr_idle_time", "3600");
    return flagMap;
  }

  @Test
  public void testPgStat() throws Exception {
    // Without warmup, each pool keeps a single backend, so the backend rows in
    // yb.orig.pg_stat_backend are deterministic. The conn mgr output is in
    // yb.orig.pg_stat_backend_1.out.
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.NONE);
    runPgRegressTest("yb_pg_stat_schedule");
  }
}
