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

import static org.junit.Assume.assumeTrue;

import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.BuildTypeUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 * Runs pg_regress tests covering planner/optimizer estimates.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressPlannerEstimates extends BasePgRegressTest {
    // (Auto-Analyze #28057) Query plans change after enabling auto analyze.
    protected Map<String, String> getTServerFlags() {
        Map<String, String> flagMap = super.getTServerFlags();
        flagMap.put("ysql_enable_auto_analyze", "false");
        return flagMap;
    }

    @Test
    public void schedule() throws Exception {
        // EXPLAIN ANALYZE RocksDB seek/next metrics are unstable in sanitizer and
        // fastdebug builds (see #22052 and #30842), making these estimate checks
        // unreliable there.
        assumeTrue("Skipping planner estimate checks on sanitizer/fastdebug builds",
            !BuildTypeUtil.isSanitizerBuild() && !BuildTypeUtil.isFastDebug());
        runPgRegressTest("yb_planner_estimates_schedule");
    }
}
