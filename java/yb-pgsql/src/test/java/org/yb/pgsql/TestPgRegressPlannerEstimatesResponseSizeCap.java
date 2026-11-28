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

import java.util.Collections;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.RequiresReleaseBuild;

/**
 * Runs the roundtrip estimate test for parallel scans whose ranges take several responses each.
 *
 * A parallel range is fetched without the session's fetch limits, and its response ends only at the
 * RPC response size cap (rpc_max_message_size * max_buffer_size_to_rpc_limit_ratio, about 229MB by
 * default), which the test tables are far too small to reach.  This class lowers the cap.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressPlannerEstimatesResponseSizeCap extends TestPgRegressPlannerEstimates {
    @Override
    @Test
    @RequiresReleaseBuild
    public void schedule() throws Exception {
        // The flag also caps the write buffer, so the tables are loaded before it is lowered.
        runPgRegressTest("yb_planner_estimates_response_size_cap_setup_schedule");

        // About 65kB with the default rpc_max_message_size.
        miniCluster.restart(true /* waitForMasterLeader */,
            Collections.singletonMap("max_buffer_size_to_rpc_limit_ratio", "0.00025"));
        connection = getConnectionBuilder().connect();

        runPgRegressTest("yb_planner_estimates_response_size_cap_schedule");
    }
}
