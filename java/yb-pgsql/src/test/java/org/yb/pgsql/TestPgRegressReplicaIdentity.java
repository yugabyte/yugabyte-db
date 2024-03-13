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
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.util.Map;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressReplicaIdentity extends BasePgSQLTest{
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("allowed_preview_flags_csv", "ysql_yb_enable_replica_identity");
    flagMap.put("ysql_yb_enable_replica_identity", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("allowed_preview_flags_csv", "ysql_yb_enable_replica_identity");
    flagMap.put("ysql_yb_enable_replica_identity", "true");
    return flagMap;
  }

  @Test
  public void testPgRegressTable() throws Exception {
    runPgRegressTest("yb_replica_identity_schedule");
  }
}
