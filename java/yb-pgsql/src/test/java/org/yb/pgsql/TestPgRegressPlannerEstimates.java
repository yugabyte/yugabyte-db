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
import org.yb.util.RequiresReleaseBuild;

/**
 * Runs pg_regress tests covering planner/optimizer estimates.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressPlannerEstimates extends BasePgRegressTest {
    // (Auto-Analyze #28057) Query plans change after enabling auto analyze.
    protected Map<String, String> getTServerFlags() {
        Map<String, String> flagMap = super.getTServerFlags();
        flagMap.put("ysql_enable_auto_analyze", "false");
        return flagMap;
    }

    // EXPLAIN ANALYZE RocksDB seek/next metrics are unstable in sanitizer and
    // fastdebug builds (see #22052 and #30842), making these estimate checks
    // unreliable there.
    @Test
    @RequiresReleaseBuild
    public void schedule() throws Exception {
        runPgRegressTest("yb_planner_estimates_schedule");
    }
}
