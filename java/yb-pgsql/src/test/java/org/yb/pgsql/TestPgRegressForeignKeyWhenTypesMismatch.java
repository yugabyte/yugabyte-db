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

@RunWith(value=YBTestRunner.class)
public class TestPgRegressForeignKeyWhenTypesMismatch extends BasePgRegressTest {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // We turn the locks on to ensure we get same results for EXPLAIN (ANALYZE)
    // queries. ysql_yb_ddl_transaction_block_enabled=true is a prerequisite for
    // enable_object_locking_for_table_locks to be true.
    flagMap.put("allowed_preview_flags_csv",
        "ysql_yb_ddl_transaction_block_enabled,enable_object_locking_for_table_locks");
    flagMap.put("ysql_yb_ddl_transaction_block_enabled", "true");
    flagMap.put("enable_object_locking_for_table_locks", "true");

    // We turn off the fastpath to ensure we get same results for EXPLAIN (ANALYZE)
    // queries on Linux and Mac, otherwise, the Storage Flush Requests are different.
    flagMap.put("enable_object_lock_fastpath", "false");
    return flagMap;
  }

  @Test
  public void testPgRegress() throws Exception {
    runPgRegressTest("yb_foreign_key_when_types_mismatch_schedule");
  }
}
