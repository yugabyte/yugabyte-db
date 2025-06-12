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

import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgRegressPartitions extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(500, 1000, 1200, 1200, 1200);
  }

  @Test
  public void misc() throws Exception {
    runPgRegressTest("yb_pg_partitions_misc_schedule");
  }

  @Test
  public void partitionwiseJoin() throws Exception {
    runPgRegressTest("yb_pg_partition_join_schedule");
  }

  @Test
  public void pruning() throws Exception {
    runPgRegressTest("yb_partition_prune_schedule");
  }

  @Test
  public void yb_partitions_tests() throws Exception {
    runPgRegressTest("yb_partitions_schedule");
  }
}
