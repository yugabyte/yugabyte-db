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
    // TODO(#26734): Enable transactional DDL (& table locks) once savepoint for DDLs are supported.
    flags.put("ysql_yb_ddl_transaction_block_enabled", "false");
    flags.put("enable_object_locking_for_table_locks", "false");
    flags.put("allowed_preview_flags_csv",
              "enable_object_locking_for_table_locks,ysql_yb_ddl_transaction_block_enabled");
    return flags;
  }

  @Test
  public void testPgRegressMatview() throws Exception {
    runPgRegressTest("yb_matview_schedule");
  }
}
