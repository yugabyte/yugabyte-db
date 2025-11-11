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
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressIsolationWithTxnDdl extends BasePgRegressTest {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("yb_enable_read_committed_isolation", "true");
    flagMap.put("ysql_yb_ddl_transaction_block_enabled", "true");
    // TODO(#28745): Enabling table locks causes the test output to be different
    // from what is expected.  When we enable table locks by default, we could
    // consider updating the expected output accordingly, to expect a
    // deadlock detection error. For now, disable table locks here.
    flagMap.put("enable_object_locking_for_table_locks", "false");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void testPgRegress() throws Exception {
    runPgRegressTest(
      PgRegressBuilder.PG_ISOLATION_REGRESS_DIR /* inputDir */, "yb_isolation_txn_ddl_schedule",
      0 /* maxRuntimeMillis */, PgRegressBuilder.PG_ISOLATION_REGRESS_EXECUTABLE);
  }
}
