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
public class TestPgRegressForeignKey extends BasePgRegressTest {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    setFailOnConflictFlags(flagMap);
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 900;
  }

  @Test
  public void testPgRegress() throws Exception {
    // The test creates single connection which all the queries are executed.
    // After reading a table, it doesn't expected extra catalog read requests
    // but in random warmup mode of conn mgr, the read request can go to any
    // backend in pool, where catalog read requests weren't made in past.
    // Therefore run the test in NONE mode, so all queries run on same backend.
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.NONE);
    runPgRegressTest("yb_foreign_key_schedule");
  }
}
