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
import org.yb.YBTestRunner;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressAggregates extends BasePgRegressTest {

  private static final String TURN_OFF_COPY_FROM_BATCH_TRANSACTION =
      "yb_default_copy_from_rows_per_transaction=0";

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    appendToYsqlPgConf(flags, TURN_OFF_COPY_FROM_BATCH_TRANSACTION);
    return flags;
  }

  @Test
  public void testPgRegressAggregates() throws Exception {
    runPgRegressTest("yb_pg_aggregates_schedule");
  }
}
