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
import org.yb.YBTestRunner;

import java.util.Map;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressMatview extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_yb_ddl_transaction_block_enabled", "true");
    flags.put("ysql_yb_enable_ddl_savepoint_support", "true");
    // Concurrent DDL requires object locking, so keep the two flags consistent.
    flags.put("enable_object_locking_for_table_locks", "true");
    flags.put("ysql_enable_concurrent_ddl", "true");
    flags.merge("allowed_preview_flags_csv", "ysql_enable_concurrent_ddl", (e, a) -> e + "," + a);
    return flags;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flags = super.getMasterFlags();
    flags.put("ysql_yb_enable_ddl_savepoint_support", "true");
    // Savepoint requires the transactional DDL flag to be enabled. Therefore, set it as well.
    flags.put("ysql_yb_ddl_transaction_block_enabled", "true");
    return flags;
  }

  @Test
  public void testPgRegressMatview() throws Exception {
    runPgRegressTest("yb_matview_schedule");
  }
}
