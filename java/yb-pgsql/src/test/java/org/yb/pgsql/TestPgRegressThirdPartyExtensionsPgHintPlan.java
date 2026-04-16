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
    // TODO(28543): Remove once transactional ddl is enabled by default.
    flagMap.put("ysql_yb_ddl_transaction_block_enabled", "true");
    flagMap.put("allowed_preview_flags_csv", "ysql_yb_ddl_transaction_block_enabled");
    // (Auto-Analyze #28057) Query plans change after enabling auto analyze.
    flagMap.put("ysql_enable_auto_analyze", "false");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/third-party-extensions/pg_hint_plan"),
                     "yb_schedule");
  }
}
