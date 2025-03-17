// Copyright (c) YugaByte, Inc.
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

import org.yb.minicluster.MiniYBClusterBuilder;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 * Runs the pg_regress test suite for DDL transaction blocks support.
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestDdlTransactionBlocks extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(500, 1000, 1200, 1200, 1200);
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enablePgTransactions(true);
    builder.addMasterFlag("vmodule", "pgsql_operation=5");
    builder.addCommonTServerFlag(
          "vmodule", "pg_client_session=5,pg_txn_manager=5,pg_client_service=5");
    builder.addCommonTServerFlag("ysql_log_statement", "all");
    builder.addCommonTServerFlag("TEST_ysql_yb_ddl_transaction_block_enabled", "true");
  }

  @Test
  public void yb_ddl_txn_block_tests() throws Exception {
    runPgRegressTest("yb_ddl_txn_block_schedule");
  }
}
