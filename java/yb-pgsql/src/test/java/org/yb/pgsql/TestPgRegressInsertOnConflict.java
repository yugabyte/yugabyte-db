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

@RunWith(value=YBTestRunner.class)
public class TestPgRegressInsertOnConflict extends BasePgRegressTest {
  public static final String YB_INSERT_ON_CONFLICT_BATCH_GUC =
      "yb_insert_on_conflict_read_batch_size";

  @Override
  public int getTestMethodTimeoutSec() {
    return 500;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // TODO(28543): Remove once transactional ddl is enabled by default.
    builder.addCommonTServerFlag("ysql_yb_ddl_transaction_block_enabled", "true");
    // (Auto Analyze #28389, #28731) Disable auto analyze: the ANALYZE statements it issues in the
    // background race with the DDL this schedule runs, and the resulting errors leak into the
    // regress output. A DDL executed inside a plain transaction block (ALTER TRIGGER from a
    // PL/pgSQL trigger function fired by INSERT ... ON CONFLICT) keeps the priority already
    // assigned to the enclosing DML transaction instead of kHighestPriority, so the FOR KEY SHARE
    // lock it takes on pg_yb_catalog_version is not guaranteed to preempt the FOR UPDATE lock held
    // by auto analyze, and the user statement fails with a 40001 conflict instead of aborting the
    // ANALYZE. Auto analyze concurrency with DDL is covered by pg_auto_analyze-test instead.
    builder.addCommonTServerFlag("ysql_enable_auto_analyze", "false");
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest("yb_insert_on_conflict_schedule");
  }
}
