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
public class TestPgRegressTypesString extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // Turn on yb_test_collation so that we get same test output for different
    // OS types (Linux vs MacOS) and different OS libc or libicu library versions.
    // This is equivalent to ALTER SYSTEM SET yb_test_collation TRUE; but YB does
    // not support ALTER SYSTEM yet so we use this trick here.
    flagMap.put("ysql_pg_conf_csv", "yb_test_collation=true");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest("yb_types_string_schedule");
  }
}
