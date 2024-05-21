// Copyright (c) YugaByteDB, Inc.
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

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressDistinctPushdown extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  /**
   * Sets the default value for both the number of masters and tservers.
   */
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  /**
   * Home for expected plans and regressions tests for
   * numerous queries affected by DISTINCT pushdown
   */
  @Test
  public void testPgRegressDistinctPushdown() throws Exception {
    runPgRegressTest("yb_distinct_pushdown_schedule");
  }
}
