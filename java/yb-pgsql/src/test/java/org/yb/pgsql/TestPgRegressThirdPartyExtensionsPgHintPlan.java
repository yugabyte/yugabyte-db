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
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;

import java.io.File;
import java.util.Map;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressThirdPartyExtensionsPgHintPlan extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // (Auto-Analyze #28057) Query plans change after enabling auto analyze.
    flagMap.put("ysql_enable_auto_analyze", "false");
    // BasePgRegressTest sets compute_query_id to regress so that EXPLAIN VERBOSE
    // in other tests does not print the query identifiers that pg_stat_statements,
    // loaded by default in YB, makes the server compute.  regress differs from
    // auto only in that EXPLAIN hides the identifier.  This schedule needs it
    // shown: get_query_id() in sql/init.sql reads it from EXPLAIN VERBOSE to fill
    // the hint table, which pg_hint_plan looks up by query identifier.  None of
    // the expected output prints an identifier, so use upstream's default, under
    // which that output was generated.
    appendToYsqlPgConf(flagMap, "compute_query_id=auto");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    // Connection Manager backend routing needs to be deterministic since the query_id warnings
    // depend on the order in which backends get attached
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.ROUND_ROBIN);
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/third-party-extensions/pg_hint_plan"),
                     "yb_schedule");
  }
}
