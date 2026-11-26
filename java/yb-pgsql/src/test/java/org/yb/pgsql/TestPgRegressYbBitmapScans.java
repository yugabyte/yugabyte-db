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

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressYbBitmapScans extends BasePgRegressTest {
  private static final int kNumShardsPerTserver = 3;

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  // The schedule builds a 145k-row, 3-index table, which under TSAN does not fit in the 1800s
  // method timeout with the default RF3 cluster: replicating every write three times and running
  // six daemons per test instance starves the host when several tests run in parallel.  RF1 cuts
  // the daemons to two and the write amplification to one copy, at the cost of no longer
  // exercising raft replication here; those races are covered by the docdb C++ tests.
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  // 3 shards on the single tserver give the same tablet count as the default 1 shard on each of
  // 3 tservers, keeping tablet boundaries, plans and row ordering unchanged.
  @Override
  protected int getNumShardsPerTServer() {
    return kNumShardsPerTserver;
  }

  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // Avoid spontaneous analyze in the middle of the test
    flagMap.put("ysql_enable_auto_analyze", "false");
    // Must be >= the tablet count so that all tablets are read concurrently, otherwise the
    // expected EXPLAIN DIST read request counts grow.  pg_doc_op derives it from the tserver
    // count, which a single tserver makes too small.
    flagMap.put("ysql_select_parallelism", Integer.toString(2 * kNumShardsPerTserver));
    return flagMap;
  }

  @Test
  public void testPgRegressYbBitmapScans() throws Exception {
    runPgRegressTest("yb_bitmap_scans_schedule");
  }
}
