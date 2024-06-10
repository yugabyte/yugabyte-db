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

@RunWith(value=YBTestRunner.class)
public class TestPgWaitQueuesRegress extends BasePgSQLTest {

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_wait_queues", "true");
    flagMap.put("yb_enable_read_committed_isolation", "true");
    /*
     * Setting yb_max_query_layer_retries allows to reliably test wait queue semantics in
     * isolation by avoiding query layer retries of serialization errors.
     */
    flagMap.put("ysql_pg_conf_csv", maxQueryLayerRetriesConf(0));
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void runPgWaitQueuesRegressTest() throws Exception {
    runPgRegressTest(
        PgRegressBuilder.PG_ISOLATION_REGRESS_DIR /* inputDir */, "yb_wait_queues_schedule",
        0 /* maxRuntimeMillis */, PgRegressBuilder.PG_ISOLATION_REGRESS_EXECUTABLE);
  }
}
