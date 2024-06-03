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
 * Runs the pg_regress test suite on colocation queries to test legacy colocated database behaviors.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressLegacyColocation extends BasePgSQLTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("ysql_legacy_colocated_database_creation", "true");
    return flagMap;
  }

  @Test
  public void testPgRegressLegacyColocation() throws Exception {
    runPgRegressTest("yb_feature_legacy_colocation_schedule");
  }
}
