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
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressThirdPartyExtensionsPgaudit extends BasePgRegressTestPorted {

  private static final String TURN_OFF_COPY_FROM_BATCH_TRANSACTION =
      "yb_default_copy_from_rows_per_transaction=0";

  @Override
  public int getTestMethodTimeoutSec() {
    return 600;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    appendToYsqlPgConf(flags, TURN_OFF_COPY_FROM_BATCH_TRANSACTION);
    flags.put("ysql_conn_mgr_alter_guc_stale_backend_ttl_ms", "-1");
    return flags;
  }

  // STATEMENT_ID in the expected output is a per-backend counter. With no warmup, each session
  // stays on one backend. A \connect gets a fresh backend only because every session before it
  // runs ALTER ROLE ... SET, which bumps the LCV and leaves all existing backends stale. The TTL
  // of -1 keeps the session that ran the ALTER on its own, now stale, backend.
  @Test
  public void schedule() throws Exception {
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.NONE);
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/third-party-extensions/pgaudit"),
                     "yb_schedule");
  }
}
