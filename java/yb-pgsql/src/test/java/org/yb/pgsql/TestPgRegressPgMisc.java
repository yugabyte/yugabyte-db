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

import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.yb.YBParameterizedTestRunner;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value = YBParameterizedTestRunner.class)
public class TestPgRegressPgMisc extends BasePgRegressTestPorted {
  // Object locking, concurrent DDL and transactional DDL are enabled/ disabled together, per the
  // cross-flag validators in common_flags.cc, so a single parameter drives all three.
  private final boolean useLegacyDDLMode;

  public TestPgRegressPgMisc(boolean useLegacyDDLMode) {
    this.useLegacyDDLMode = useLegacyDDLMode;
  }

  @Parameterized.Parameters(name = "useLegacyDDLMode={0}")
  public static List<Object[]> parameters() {
    return Arrays.asList(new Object[]{true}, new Object[]{false});
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  // Disable auto analyze likely because of issue #27973.
  // This may not be related to auto analyze at all.
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_enable_auto_analyze", "false");
    toggleDDLMode(flagMap, useLegacyDDLMode);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    toggleDDLMode(flagMap, useLegacyDDLMode);
    return flagMap;
  }

  @Test
  public void testPgRegressPgMisc() throws Exception {
    runPgRegressTest("yb_pg_misc_serial_schedule");
  }
}
