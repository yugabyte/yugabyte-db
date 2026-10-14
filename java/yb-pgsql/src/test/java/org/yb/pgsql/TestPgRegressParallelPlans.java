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
import org.yb.util.RequiresReleaseBuild;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressParallelPlans extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    // (Auto-Analyze #28057) Query plans change after enabling auto analyze.
    flags.put("ysql_enable_auto_analyze", "false");
    flags.put("yb_enable_read_committed_isolation", "false");
    // The default block cache is 50% of the mini cluster's 1GB memory limit (which cannot be
    // raised: MiniYBCluster appends memory_limit_hard_bytes after these flags), and scanning the
    // 1.5GB t1m table fills it, leaving too little for everything else.
    flags.put("db_block_cache_size_bytes", String.valueOf(128L * 1024 * 1024));
    // Let log GC reclaim the t1m load's WAL while it is being written.  The 900s default outlives
    // the test, so every segment is retained and the host free space can fall under
    // reject_writes_min_disk_space_pct.  A low retention needs the xCluster staleness check
    // disabled (0 bypasses it), or every tserver fails flag validation at startup.
    flags.put("log_min_seconds_to_retain", "10");
    flags.put("xcluster_checkpoint_max_staleness_secs", "0");
    // TODO(#33505): yb.orig.parallel_force_guc creates a function inside a transaction block and
    // then forces a parallel plan over it. With concurrent DDL the catalog entry is still
    // uncommitted and the parallel worker does not see it (cache lookup failed for function ...,
    // CONTEXT: parallel worker). Run in the legacy mode until parallel workers pick up the
    // leader's uncommitted DDL.
    toggleDDLMode(flags, /* useLegacy */ true);
    return flags;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    toggleDDLMode(flagMap, /* useLegacy */ true);
    return flagMap;
  }

  // Complex parallel query plans may timeout on slow builds
  @Test
  @RequiresReleaseBuild
  public void schedule() throws Exception {
    runPgRegressTest("yb_parallel_plans_schedule");
  }
}
