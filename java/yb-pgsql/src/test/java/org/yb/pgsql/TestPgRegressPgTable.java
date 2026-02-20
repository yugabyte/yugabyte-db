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

import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.yb.util.YBParameterizedTestRunnerNonTsanOnly;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value = YBParameterizedTestRunnerNonTsanOnly.class)
public class TestPgRegressPgTable extends BasePgRegressTestPorted {
  private final boolean concurrentDDLEnabled;

  public TestPgRegressPgTable(boolean concurrentDDLEnabled) {
    this.concurrentDDLEnabled = concurrentDDLEnabled;
  }

  @Parameterized.Parameters(name = "concurrentDDLEnabled={0}")
  public static List<Boolean> parameters() {
    return Arrays.asList(false, true);
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // (Auto-Analyze #28393) error output is flaky.
    flagMap.put("ysql_enable_auto_analyze", "false");
    appendToYsqlPgConf(
        flagMap, "yb_fallback_to_legacy_catalog_read_time=" + !concurrentDDLEnabled);
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest("yb_pg_table_schedule");
  }
}
