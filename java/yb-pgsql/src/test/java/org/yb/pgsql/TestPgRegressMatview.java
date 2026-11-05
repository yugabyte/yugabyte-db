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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import java.util.Map;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressMatview extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    toggleDDLMode(flags, /* useLegacy */ false);
    // The schedule exercises DDL inside savepoints, so the support has to be on in every
    // build type.
    flags.put("ysql_yb_enable_ddl_savepoint_support", "true");
    return flags;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flags = super.getMasterFlags();
    toggleDDLMode(flags, /* useLegacy */ false);
    // The schedule exercises DDL inside savepoints, so the support has to be on in every
    // build type.
    flags.put("ysql_yb_enable_ddl_savepoint_support", "true");
    return flags;
  }

  @Test
  public void testPgRegressMatview() throws Exception {
    runPgRegressTest("yb_matview_schedule");
  }
}
