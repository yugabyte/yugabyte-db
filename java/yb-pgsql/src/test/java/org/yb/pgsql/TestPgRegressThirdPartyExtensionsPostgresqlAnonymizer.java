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

import java.io.File;

import java.sql.Connection;
import java.sql.Statement;

import java.util.Map;
import java.util.Collections;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.BuildTypeUtil;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestPgRegressThirdPartyExtensionsPostgresqlAnonymizer extends BasePgRegressTest {

  private static final File regress_schedule = new File(
      TestUtils.getBuildRootDir(),
      "postgres_build/third-party-extensions/postgresql_anonymizer/tests");

  @Override
  public int getTestMethodTimeoutSec() {
    return (int) BuildTypeUtil.adjustTimeout(1800);
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "shared_preload_libraries='anon'");
    // to avoid batched copy warning when running anon.init() or
    // anon.start_dynamic_masking() inside a transaction
    appendToYsqlPgConf(flagMap, "yb_default_copy_from_rows_per_transaction=0");
    flagMap.put("TEST_generate_ybrowid_sequentially", "true");
    // this is the default isolation level in upstream tests
    flagMap.put("yb_enable_read_committed_isolation", "true");
    return flagMap;
  }

  @Test
  public void schedule1() throws Exception {
    runPgRegressTest(regress_schedule, "yb_schedule_1");
  }

  @Test
  public void schedule2() throws Exception {
    runPgRegressTest(regress_schedule, "yb_schedule_2");
  }

  @Test
  public void schedule3() throws Exception {
    runPgRegressTest(regress_schedule, "yb_schedule_3");
  }
}
