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
import java.util.Map;

import org.apache.commons.io.FileUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressThirdPartyExtensionsHll extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // hll's planner hook (hll.force_groupagg) exists only after hll.so is loaded; with the
    // connection manager, a query may run on a backend that has not loaded it yet.
    appendToYsqlPgConf(flagMap, "shared_preload_libraries='hll'");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    File buildDir = new File(
      TestUtils.getBuildRootDir(),
      "postgres_build/third-party-extensions/postgresql-hll");
    // psql runs in the pg_regress input dir, where copy_binary writes binary.dat.  Use a private
    // copy so concurrent runs against one build dir do not overwrite each other's file.
    File regress_schedule = new File(TestUtils.getBaseTmpDir(), "postgresql-hll");
    for (String name : new String[] {"sql", "expected", "yb_schedule"}) {
      FileUtils.copyToDirectory(new File(buildDir, name), regress_schedule);
    }
    runPgRegressTest(regress_schedule, "yb_schedule");
  }
}
