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

import java.util.Collections;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgIsolationRegress extends BasePgSQLTest {

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("yb_enable_read_committed_isolation", "true");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  private void runIsolationRegressTest() throws Exception {
    runPgRegressTest(
        PgRegressBuilder.PG_ISOLATION_REGRESS_DIR /* inputDir */, "yb_pg_isolation_schedule",
        0 /* maxRuntimeMillis */, PgRegressBuilder.PG_ISOLATION_REGRESS_EXECUTABLE);
  }

  @Test
  public void isolationRegress() throws Exception {
    runIsolationRegressTest();
  }

  @Test
  public void withDelayedTxnApply() throws Exception {
    // The reason for running all tests in the schedule again with
    // TEST_inject_sleep_before_applying_intents_ms is the following: our tests usually have very
    // small transactions such that the step of applying intents to regular db after commit is
    // instantaneous enough that the code path which checks and resolves conflicts with committed
    // transactions (that are not yet applied), is not exercised. To ensure we exercise this rare
    // code path of performing conflict resolution with committed but not yet applied transactions,
    // we inject a sleep before applying intents.
    restartClusterWithFlags(Collections.emptyMap(),
                            Collections.singletonMap("TEST_inject_sleep_before_applying_intents_ms",
                                                     "100"));
    runIsolationRegressTest();
    // Revert back to old set of flags for other test methods
    restartClusterWithFlags(Collections.emptyMap(), Collections.emptyMap());
  }
}
