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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

// Runs the pg_regress test suite on YB code.
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressIndex extends BasePgRegressTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgRegressIndex.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("TEST_ysql_suppress_ybctid_corruption_details", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // TODO (#19975): Enable read committed isolation
    flagMap.put("yb_enable_read_committed_isolation", "false");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    // (DB-13032) This test touches system tables, so enable stickiness for
    // superuser connections when Connection Manager is enabled.
    enableStickySuperuserConnsAndRestartCluster();
    runPgRegressTest("yb_index_schedule");
  }
}
