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

@RunWith(value = YBTestRunner.class)
public class TestPgRegressForeignKeyWhenTypesMismatch extends BasePgRegressTest {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();

    // Force GUC on so the tests pass when backported to releases
    // where the compile-time default is set to off by default.
    appendToYsqlPgConf(flagMap, "yb_enable_fkey_batched_docdb_lookup_when_types_mismatch=true");

    // We turn table-level locks on to ensure we get same results for EXPLAIN (ANALYZE) queries.
    toggleDDLMode(flagMap, /* useLegacy */ false);

    // We turn off the fastpath to ensure we get same results for EXPLAIN (ANALYZE)
    // queries on Linux and Mac, otherwise, the Storage Flush Requests are different.
    flagMap.put("enable_object_lock_fastpath", "false");

    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    toggleDDLMode(flagMap, /* useLegacy */ false);
    return flagMap;
  }

  @Test
  public void testPgRegress() throws Exception {
    // Set connection manager mode to NONE to ensure
    // we get same results for EXPLAIN (ANALYZE) queries.
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.NONE);

    runPgRegressTest("yb_foreign_key_when_types_mismatch_schedule");
  }
}
