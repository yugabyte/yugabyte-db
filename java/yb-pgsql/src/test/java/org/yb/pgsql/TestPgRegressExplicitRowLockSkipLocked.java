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

@RunWith(value=YBTestRunner.class)
public class TestPgRegressExplicitRowLockSkipLocked extends BasePgRegressTest {
  public static final String YB_EXPLICIT_ROW_LOCK_SKIP_LOCKED_MAX_READ_AHEAD_GUC =
      "yb_explicit_row_lock_skip_locked_max_read_ahead";

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // Pin packed row so INSERT lock modes in yb_lock_status are stable across build types.
    flagMap.put("ysql_enable_packed_row", "true");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 600;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest("yb_explicit_row_lock_skip_locked_schedule");
  }
}
