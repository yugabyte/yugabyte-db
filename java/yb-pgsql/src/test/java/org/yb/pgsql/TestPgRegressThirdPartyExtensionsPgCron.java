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

import java.io.File;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

// Disable in TSAN since it times out on pg_cron exist #22295.
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgRegressThirdPartyExtensionsPgCron extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("allowed_preview_flags_csv", "enable_pg_cron");
    flagMap.put("enable_pg_cron", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    if (isTestRunningWithConnectionManager()) {
      flagMap.put("allowed_preview_flags_csv",
            "enable_pg_cron,enable_ysql_conn_mgr");
    }
    else {
      flagMap.put("allowed_preview_flags_csv", "enable_pg_cron");
    }
    flagMap.put("enable_pg_cron", "true");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest(
        new File(TestUtils.getBuildRootDir(), "postgres_build/third-party-extensions/pg_cron"),
        "yb_schedule");
  }
}
