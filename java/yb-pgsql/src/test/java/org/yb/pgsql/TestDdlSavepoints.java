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
 * Runs the pg_regress test suite for DDL savepoint support.
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestDdlSavepoints extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(500, 1000, 1200, 1200, 1200);
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enablePgTransactions(true);
    builder.addCommonTServerFlag("ysql_log_statement", "all");
    builder.addCommonTServerFlag("ysql_yb_ddl_transaction_block_enabled", "true");
    builder.addCommonTServerFlag("enable_object_locking_for_table_locks", "true");
    builder.addCommonTServerFlag("ysql_yb_enable_ddl_savepoint_support", "true");
    builder.addCommonTServerFlag(
        "allowed_preview_flags_csv", "ysql_yb_enable_ddl_savepoint_support");
    builder.addMasterFlag("ysql_yb_ddl_transaction_block_enabled", "true");
    builder.addMasterFlag("ysql_yb_enable_ddl_savepoint_support", "true");
    builder.addMasterFlag("allowed_preview_flags_csv", "ysql_yb_enable_ddl_savepoint_support");
    builder.addMasterFlag("vmodule", "catalog_manager=3,ysql_ddl_handler=4");
  }

  @Test
  public void yb_ddl_savepoint_tests() throws Exception {
    // GH-27235 - There are issues with schema invalidation on other backends
    // for rolled back transactional DDLs, causing the test to fail with
    // Connection Mangaer enabled. Force the test to operate on one physical
    // connection until this issue is resolved.
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.NONE);
    runPgRegressTest("yb_ddl_savepoint_schedule");
  }
}
