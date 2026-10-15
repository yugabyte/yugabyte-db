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

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressPgMiscIndependent extends BasePgRegressTestPorted {

  private static final int TURN_OFF_SEQUENCE_CACHE_FLAG = 0;

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_sequence_cache_minval", Integer.toString(TURN_OFF_SEQUENCE_CACHE_FLAG));
    // TODO(#26734): yb.port.namespace, yb.port.truncate, yb.port.sequence and
    // yb.port.alter_generic carry statements that work around DDL not being rolled back with the
    // transaction (a manual DROP SCHEMA, re-INSERTs after a rolled back TRUNCATE, manual DROP
    // SEQUENCE / DROP FUNCTION). With concurrent DDL the rollback does undo the DDL, so those
    // workarounds fail. Run in the legacy mode until the expected output covers both modes.
    toggleDDLMode(flagMap, /* useLegacy */ true);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    toggleDDLMode(flagMap, /* useLegacy */ true);
    return flagMap;
  }

  @Test
  public void testPgRegressPgMiscIndependent() throws Exception {
    runPgRegressTest("yb_pg_misc_independent_1_schedule");
  }

  @Test
  public void testPgRegressPgMiscIndependent2() throws Exception {
    runPgRegressTest("yb_pg_misc_independent_2_schedule");
  }
}
