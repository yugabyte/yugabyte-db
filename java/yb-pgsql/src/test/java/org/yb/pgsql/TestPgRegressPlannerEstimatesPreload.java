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
 * Runs pg_regress tests covering the planner/optimizer estimates that depend on catalog
 * preloading.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressPlannerEstimatesPreload extends BasePgRegressTest {
    protected Map<String, String> getTServerFlags() {
        Map<String, String> flagMap = super.getTServerFlags();
        // (Auto-Analyze #28057) Query plans change after enabling auto analyze.
        flagMap.put("ysql_enable_auto_analyze", "false");
        // Every new backend, a parallel worker included, preloads its catalog caches.
        flagMap.put("ysql_catalog_preload_additional_tables", "true");
        return flagMap;
    }

    // The estimates suite this belongs with runs in release builds only.
    @Test
    @RequiresReleaseBuild
    public void schedule() throws Exception {
        runPgRegressTest("yb_planner_estimates_preload_schedule");
    }
}
