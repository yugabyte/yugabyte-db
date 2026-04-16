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
import org.yb.minicluster.MiniYBClusterBuilder;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressPgTypesUDT extends BasePgRegressTestPorted {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // TODO(28543): Remove once transactional ddl is enabled by default.
    builder.addCommonTServerFlag("ysql_yb_ddl_transaction_block_enabled", "true");
    builder.addCommonTServerFlag(
        "allowed_preview_flags_csv", "ysql_yb_ddl_transaction_block_enabled");
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest("yb_pg_types_udt_serial_schedule");
  }
}
