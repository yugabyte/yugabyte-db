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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.BuildTypeUtil;
import org.yb.util.ConfForTesting;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestPgRegressThirdPartyExtensionsPostgresqlAnonymizer extends BasePgRegressTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgRegressThirdPartyExtensionsPostgresqlAnonymizer.class);

  private static final File regress_schedule = new File(
      TestUtils.getBuildRootDir(),
      "postgres_build/third-party-extensions/postgresql_anonymizer/tests");

  @Override
  public int getTestMethodTimeoutSec() {
    return (int) BuildTypeUtil.adjustTimeout(1800);
  }

  // Each test creates and drops the extension's many tables; RF 1 keeps the tablet count low
  // enough for a schedule to finish within the 30-minute test timeout cap on TSAN.
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_pg_anonymizer", "true");
    // to avoid batched copy warning when running anon.init() or
    // anon.start_dynamic_masking() inside a transaction
    appendToYsqlPgConf(flagMap, "yb_default_copy_from_rows_per_transaction=0");
    flagMap.put("TEST_generate_ybrowid_sequentially", "true");
    // this is the default isolation level in upstream tests
    flagMap.put("yb_enable_read_committed_isolation", "true");
    // (Auto Analyze #28391) Restarting a DDL transaction not supported
    flagMap.put("ysql_enable_auto_analyze", "false");
    // TODO(#26734): with concurrent DDL, anon.reset() fails in yb.port.init because its TRUNCATE
    // now runs inside a transaction block and the test sets yb_enable_alter_table_rewrite=false
    // ("unsafe TRUNCATE cannot be executed in a transaction block"), and yb.port.injection's
    // manual DROP TABLE workarounds fail once the rollback undoes the DDL. Run in the legacy mode
    // until both are addressed.
    toggleDDLMode(flagMap, /* useLegacy */ true);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    toggleDDLMode(flagMap, /* useLegacy */ true);
    return flagMap;
  }

  // pg_regress runs in its input dir, and tests write scratch files (e.g. tmp/_pg_dump_A.sql)
  // relative to it and read ../data, so give each run a private copy to avoid clashing with
  // concurrent runs from the same build.
  private File privateInputDir() throws Exception {
    File root = new File(TestUtils.getBaseTmpDir(), "postgresql_anonymizer");
    FileUtils.copyDirectory(regress_schedule.getParentFile(), root);
    return new File(root, "tests");
  }

  private void runSchedule(String schedule) throws Exception {
    File inputDir = privateInputDir();
    try {
      runPgRegressTest(inputDir, schedule);
    } finally {
      // Outside CI, PgRegressRunner copies results back into the input dir. Move them on to the
      // build dir, where developers look for them, since the tmp dir may be deleted on exit.
      if (!ConfForTesting.isCI()) {
        File results = new File(inputDir, "results");
        File diffs = new File(inputDir, "regression.diffs");
        LOG.info("Copying pg_regress results from {} to {}", inputDir, regress_schedule);
        if (results.isDirectory()) {
          FileUtils.copyDirectory(results, new File(regress_schedule, "results"));
        }
        if (diffs.isFile()) {
          FileUtils.copyFileToDirectory(diffs, regress_schedule);
        }
      }
    }
  }

  @Test
  public void schedule1() throws Exception {
    runSchedule("yb_1_schedule");
  }

  @Test
  public void schedule2() throws Exception {
    runSchedule("yb_2_schedule");
  }

  @Test
  public void schedule3() throws Exception {
    runSchedule("yb_3_schedule");
  }
}
