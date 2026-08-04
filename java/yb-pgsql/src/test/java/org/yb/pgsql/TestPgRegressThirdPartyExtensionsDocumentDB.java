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
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonSanOrAArch64Mac;

// (#26948): Builds like those on ubuntu22.04 do not support the extension and we currently have
// no test runner to detect this.
@Ignore("Disabled until fix for #26948 lands")
@RunWith(value = YBTestRunnerNonSanOrAArch64Mac.class)
public class TestPgRegressThirdPartyExtensionsDocumentDB extends BasePgRegressTest {
  @Before
  public void setUp() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE EXTENSION IF NOT EXISTS documentdb CASCADE"));
    }
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("enable_pg_cron", "true");
    flagMap.put("allowed_preview_flags_csv", "ysql_enable_documentdb");
    flagMap.put("ysql_enable_documentdb", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_pg_cron", "true");
    flagMap.put("allowed_preview_flags_csv", "ysql_enable_documentdb");
    flagMap.put("ysql_enable_documentdb", "true");
    flagMap.put("ysql_suppress_unsafe_alter_notice", "true");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest(
        new File(TestUtils.getBuildRootDir(),
            "postgres_build/third-party-extensions/documentdb/pg_documentdb/src/test/regress"),
        "yb_schedule");
  }
}
