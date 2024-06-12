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


import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.*;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressBackfillIndex extends BasePgRegressTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgRegressBackfillIndex.class);

  private static final String TURN_OFF_COPY_FROM_BATCH_TRANSACTION =
      "yb_default_copy_from_rows_per_transaction=0";
  private static final String database = "yugabyte";
  private static final int TEST_TIMEOUT = 120000;

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  /*
   * We use the following parameters to enable tablet splitting aggressively.
   */
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_prefetch_limit", Integer.toString(1024));
    flags.put("backfill_index_write_batch_size", Integer.toString(5 * 1024));
    return flags;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flags = super.getMasterFlags();
    flags.put("ysql_disable_index_backfill", "false");
    flags.put("ysql_index_backfill_rpc_timeout_ms", Integer.toString(60 * 1000)); // 1 min.
    return flags;
  }

  @Test
  public void testPgRegressBackfillIndex() throws Exception {
    final String table = "airports";
    // 1. Creates a large table (airports)
    // 2. Copies content from a csv file
    // 3. Executes a bunch of select operations
    runPgRegressTest("yb_large_table_backfill_index_schedule");
  }
}
