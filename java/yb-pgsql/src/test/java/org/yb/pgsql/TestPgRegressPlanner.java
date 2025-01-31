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
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressPlanner extends BasePgRegressTest {
    @Override
    public int getTestMethodTimeoutSec() {
        return 1800;
    }

    @Test
    public void testPgRegressPlanner() throws Exception {
        // (DB-13032) This test touches system tables, so enable stickiness for
        // superuser connections when Connection Manager is enabled.
        enableStickySuperuserConnsAndRestartCluster();
        runPgRegressTest("yb_planner_serial_schedule");
    }
}
