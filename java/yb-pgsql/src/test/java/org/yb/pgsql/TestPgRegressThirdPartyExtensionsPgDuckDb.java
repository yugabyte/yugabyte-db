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
import java.sql.Statement;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;
import org.yb.util.SkipOnASAN;
import org.yb.util.SkipOnTSAN;

/*
* pg_duckdb links a prebuilt, uninstrumented DuckDB bundle, it is not built on sanitizer
* (ASAN/TSAN) builds: CMakeLists.txt defines YB_ENABLE_YSQL_PG_DUCKDB_EXT only on non-sanitizer
* builds, and ValidatePgDuckDB rejects ysql_yb_enable_pg_duckdb=true there. The extension does not
* exist to test under ASAN/TSAN, so skip this suite on both.
*/
@SkipOnASAN
@SkipOnTSAN
@RunWith(value = YBTestRunner.class)
public class TestPgRegressThirdPartyExtensionsPgDuckDb extends BasePgRegressTest {
  @Before
  public void setUp() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE EXTENSION IF NOT EXISTS pg_duckdb"));
    }
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("allowed_preview_flags_csv", "ysql_yb_enable_pg_duckdb");
    flagMap.put("ysql_yb_enable_pg_duckdb", "true");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest(
        new File(TestUtils.getBuildRootDir(),
            "postgres_build/third-party-extensions/pg_duckdb/test/regression"),
        "yb_schedule");
  }
}
