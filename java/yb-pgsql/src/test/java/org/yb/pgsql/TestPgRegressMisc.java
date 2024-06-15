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
import org.yb.YBTestRunner;

import java.util.Map;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressMisc extends BasePgRegressTest {
  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("TEST_sequential_colocation_ids", "true");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void testPgRegressMiscIndependent() throws Exception {
    runPgRegressTest("yb_misc_independent_schedule");
  }

  @Test
  public void testPgRegressMiscSerial() throws Exception {
    runPgRegressTest("yb_misc_serial_schedule");
  }

  @Test
  public void testPgRegressMiscSerial2() throws Exception {
    runPgRegressTest("yb_misc_serial2_schedule");
  }

  @Test
  public void testPgRegressMiscSerial3() throws Exception {
    runPgRegressTest("yb_misc_serial3_schedule");
  }

  @Test
  public void testPgRegressMiscSerial4() throws Exception {
    runPgRegressTest("yb_misc_serial4_schedule");
  }
}
